package main

import (
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	toml "github.com/pelletier/go-toml/v2"
)

// MaxTomlOverlayBytes is the maximum size for RunPod input.toml_overlay (WUI enforces before POST).
const MaxTomlOverlayBytes = 64 * 1024

// TrainingRequest is defined in main.go (POST /api/runs body).

func enclaveTierName(tier int) (string, error) {
	switch tier {
	case 0:
		return "", nil
	case 1:
		return "micro", nil
	case 2:
		return "meso", nil
	case 3:
		return "macro", nil
	case 4:
		return "workgroup", nil
	case 5:
		return "enterprise_core", nil
	default:
		return "", fmt.Errorf("invalid enclave_tier %d", tier)
	}
}

func edgeProfileActive(req *TrainingRequest) bool {
	if req == nil {
		return false
	}
	if req.EnclaveTier != 0 || req.MemoryLimitMB > 0 {
		return true
	}
	return strings.TrimSpace(req.HardwareAccelerator) != ""
}

// buildEdgeProfileExtraEnv appends env vars for Tier-1 edge (lighter tropical attention path).
// tierDefaultFootprintMB matches qminiwasm.config.ENCLAVE_TIER_PRESETS ef_target_mb.
func tierDefaultFootprintMB(tier int) float64 {
	switch tier {
	case 1:
		return 250
	case 2:
		return 2048
	case 3:
		return 8192
	case 4:
		return 16384
	case 5:
		return 262144
	default:
		return 0
	}
}

func buildEdgeProfileExtraEnv(req *TrainingRequest) []string {
	if req == nil {
		return nil
	}
	var out []string
	if req.EnclaveTier == 1 {
		out = append(out, "QMW_DISABLE_TROPICAL_ATTN=1")
	}
	a := strings.ToLower(strings.TrimSpace(req.HardwareAccelerator))
	if a == "quantum_mesh" {
		out = append(out, "QMW_QUANTUM_MESH_ROUTING=1")
	}
	return out
}

func readTrainingBatchSize(baseAbs string) (int, error) {
	raw, err := os.ReadFile(baseAbs)
	if err != nil {
		return 0, err
	}
	var doc map[string]any
	if err := toml.Unmarshal(raw, &doc); err != nil {
		return 0, err
	}
	tr, _ := doc["training"].(map[string]any)
	if tr == nil {
		return 32, nil
	}
	return coerceTomlInt(tr["batch_size"], 32), nil
}

func coerceTomlInt(v any, def int) int {
	switch x := v.(type) {
	case int64:
		return int(x)
	case int:
		return x
	case int32:
		return int(x)
	case float64:
		return int(x)
	default:
		return def
	}
}

// buildEdgeTomlOverlay returns a TOML fragment (no secrets) from edge profiling fields.
// baseConfigAbs is used to read training.batch_size for tier 3+ batch bump.
func buildEdgeTomlOverlay(req *TrainingRequest, baseConfigAbs string) (string, error) {
	if req == nil {
		return "", nil
	}
	if err := req.validateEdgeFields(); err != nil {
		return "", err
	}
	if !edgeProfileActive(req) {
		return "", nil
	}

	tier := req.EnclaveTier
	memMB := req.MemoryLimitMB
	if memMB == 0 && tier >= 2 && tier <= 5 {
		memMB = int(tierDefaultFootprintMB(tier))
	}

	accel := strings.ToLower(strings.TrimSpace(req.HardwareAccelerator))
	tomAccel := accel
	if accel == "sycl" {
		tomAccel = "xpu"
	}
	if accel == "quantum_mesh" {
		tomAccel = "cpu"
	}

	var b strings.Builder
	if accel != "" {
		b.WriteString("[hardware]\n")
		b.WriteString("accelerator = " + strconv.Quote(tomAccel) + "\n")
		if accel == "quantum_mesh" {
			b.WriteString("quantum_execution_policy = \"prefer_hardware_fallback\"\n")
		}
		b.WriteString("\n")
	}

	switch {
	case tier == 1:
		wasmCap := 256
		if memMB > 0 {
			wasmCap = memMB
		}
		ef := 250.0
		if memMB > 0 {
			ef = float64(memMB)
		}
		b.WriteString("[adapter]\n")
		b.WriteString("use_tsign_ternary = true\n")
		b.WriteString("hybrid_adapter = false\n")
		b.WriteString("lota_rank = 0\n\n")
		b.WriteString("[wasm]\n")
		b.WriteString(fmt.Sprintf("store_memory_limit_mb = %d\n\n", wasmCap))
		b.WriteString("[enclave]\n")
		b.WriteString("enclave_tier = \"micro\"\n")
		b.WriteString("use_memory64 = false\n")
		b.WriteString(fmt.Sprintf("enclave_footprint_mb = %g\n\n", ef))
		baseBS, err := readTrainingBatchSize(baseConfigAbs)
		if err != nil {
			baseBS = 32
		}
		if baseBS > 16 {
			b.WriteString("[training]\n")
			b.WriteString("batch_size = 16\n\n")
		}

	case tier >= 3:
		name, err := enclaveTierName(tier)
		if err != nil {
			return "", err
		}
		b.WriteString("[enclave]\n")
		b.WriteString(fmt.Sprintf("enclave_tier = %q\n", name))
		b.WriteString("use_memory64 = true\n")
		if memMB > 0 {
			b.WriteString(fmt.Sprintf("wasm_memory64_max_mb = %g\n", float64(memMB)))
			b.WriteString(fmt.Sprintf("enclave_footprint_mb = %g\n", float64(memMB)))
		}
		b.WriteString("\n")
		if memMB > 0 {
			b.WriteString("[wasm]\n")
			b.WriteString(fmt.Sprintf("store_memory_limit_mb = %d\n\n", memMB))
		}
		baseBS, err := readTrainingBatchSize(baseConfigAbs)
		if err != nil {
			baseBS = 32
		}
		newBS := baseBS * 2
		if newBS < 32 {
			newBS = 32
		}
		if newBS > 128 {
			newBS = 128
		}
		if newBS != baseBS {
			b.WriteString("[training]\n")
			b.WriteString(fmt.Sprintf("batch_size = %d\n\n", newBS))
		}

	case tier == 2:
		b.WriteString("[enclave]\n")
		b.WriteString("enclave_tier = \"meso\"\n")
		b.WriteString("use_memory64 = false\n")
		if memMB > 0 {
			b.WriteString(fmt.Sprintf("enclave_footprint_mb = %g\n", float64(memMB)))
		}
		b.WriteString("\n")
		if memMB > 0 {
			b.WriteString("[wasm]\n")
			b.WriteString(fmt.Sprintf("store_memory_limit_mb = %d\n\n", memMB))
		}

	default:
		// tier == 0: memory / accelerator only
		if memMB > 0 {
			b.WriteString("[wasm]\n")
			b.WriteString(fmt.Sprintf("store_memory_limit_mb = %d\n\n", memMB))
			b.WriteString("[enclave]\n")
			b.WriteString(fmt.Sprintf("enclave_footprint_mb = %g\n\n", float64(memMB)))
		}
	}

	s := strings.TrimSpace(b.String())
	if len(s) > MaxTomlOverlayBytes {
		return "", fmt.Errorf("edge profile overlay exceeds %d bytes", MaxTomlOverlayBytes)
	}
	return s, nil
}

func deepMergeTomlMaps(dst, src map[string]any) {
	for k, sv := range src {
		dv, ok := dst[k]
		if !ok {
			dst[k] = sv
			continue
		}
		dmap, dOk := dv.(map[string]any)
		smap, sOk := sv.(map[string]any)
		if dOk && sOk {
			deepMergeTomlMaps(dmap, smap)
			dst[k] = dmap
		} else {
			dst[k] = sv
		}
	}
}

func mergeTrainingTomlBytes(base []byte, overlay string) ([]byte, error) {
	if strings.TrimSpace(overlay) == "" {
		return base, nil
	}
	var baseMap map[string]any
	if err := toml.Unmarshal(base, &baseMap); err != nil {
		return nil, fmt.Errorf("parse base TOML: %w", err)
	}
	if baseMap == nil {
		baseMap = map[string]any{}
	}
	var overMap map[string]any
	if err := toml.Unmarshal([]byte(overlay), &overMap); err != nil {
		return nil, fmt.Errorf("parse overlay TOML: %w", err)
	}
	deepMergeTomlMaps(baseMap, overMap)
	out, err := toml.Marshal(baseMap)
	if err != nil {
		return nil, fmt.Errorf("marshal merged TOML: %w", err)
	}
	return out, nil
}

func mergeTrainingTomlFile(baseAbs, overlay string) ([]byte, error) {
	raw, err := os.ReadFile(baseAbs)
	if err != nil {
		return nil, err
	}
	return mergeTrainingTomlBytes(raw, overlay)
}

func writeMergedEdgeTrainingConfig(repoRoot, baseAbs, overlay, relStem string) (abs string, rel string, err error) {
	if strings.TrimSpace(overlay) == "" {
		return "", "", errors.New("empty overlay")
	}
	merged, err := mergeTrainingTomlFile(baseAbs, overlay)
	if err != nil {
		return "", "", err
	}
	tag := relStem
	if tag == "" {
		b := make([]byte, 6)
		_, _ = rand.Read(b)
		tag = hex.EncodeToString(b)
	}
	rel = filepath.ToSlash(filepath.Join("configs", "training", ".wui_edge_"+tag+".toml"))
	abs = filepath.Join(repoRoot, filepath.FromSlash(rel))
	if err := os.MkdirAll(filepath.Dir(abs), 0o755); err != nil {
		return "", "", err
	}
	if err := os.WriteFile(abs, merged, 0o644); err != nil {
		return "", "", err
	}
	return abs, rel, nil
}
