package main

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"

	toml "github.com/pelletier/go-toml/v2"
)

// nativeHFSpec is one Hub dataset row from Step 1 / Dataset Builder (JSON hf_specs).
type nativeHFSpec struct {
	Path          string `json:"path"`
	DatasetConfig string `json:"dataset_config"`
}

const cascadeNativeBaseRel = "configs/training/cascade_curriculum_native.toml"

// nativeCascadeOverlayRel returns the repo-relative path to the tier overlay TOML.
func nativeCascadeOverlayRel(tier int) (string, error) {
	switch tier {
	case 1:
		return "configs/training/native_cascade/tier_edge_constrained.toml", nil
	case 2:
		return "configs/training/native_cascade/tier_fog_node.toml", nil
	case 3, 4, 5:
		return "configs/training/native_cascade/tier_xpu_cluster.toml", nil
	default:
		return "", fmt.Errorf("enclave_tier must be 1–5, got %d", tier)
	}
}

func buildNativeCascadeMergedBytes(enclaveTier int) (merged []byte, overlayRel string, err error) {
	overlayRel, err = nativeCascadeOverlayRel(enclaveTier)
	if err != nil {
		return nil, "", err
	}
	baseAbs := filepath.Join(repoRoot, filepath.FromSlash(cascadeNativeBaseRel))
	overAbs := filepath.Join(repoRoot, filepath.FromSlash(overlayRel))
	if _, err := os.Stat(baseAbs); err != nil {
		return nil, overlayRel, fmt.Errorf("base cascade config: %w", err)
	}
	if _, err := os.Stat(overAbs); err != nil {
		return nil, overlayRel, fmt.Errorf("tier overlay: %w", err)
	}
	merged, err = mergeTrainingTomlFromFiles(baseAbs, overAbs)
	if err != nil {
		return nil, overlayRel, err
	}
	merged, err = forceEnclaveTierInToml(merged, enclaveTier)
	if err != nil {
		return nil, overlayRel, err
	}
	return merged, overlayRel, nil
}

func huggingFaceNumSamplesFromToml(raw []byte) int64 {
	var root map[string]any
	if err := toml.Unmarshal(raw, &root); err != nil || root == nil {
		return 0
	}
	hf, _ := root["huggingface"].(map[string]any)
	if hf == nil {
		return 0
	}
	v, ok := hf["num_samples"]
	if !ok || v == nil {
		return 0
	}
	switch x := v.(type) {
	case int64:
		return x
	case int:
		return int64(x)
	case float64:
		return int64(x)
	default:
		return 0
	}
}

func effectiveNativeCascadeNumSamples(tierSamples int64, targetSamples int) int64 {
	ts := tierSamples
	if ts < 0 {
		ts = 0
	}
	if targetSamples <= 0 {
		return ts
	}
	t := int64(targetSamples)
	if ts <= 0 {
		return t
	}
	if t < ts {
		return t
	}
	return ts
}

func quoteTomlString(s string) string {
	s = strings.ReplaceAll(s, "\\", "\\\\")
	s = strings.ReplaceAll(s, "\"", "\\\"")
	return `"` + s + `"`
}

// applyDatasetMixToMergedToml overlays Step 1 hf_specs onto merged native-cascade TOML.
// When len(specs)==0, returns merged unchanged (datasetMixApplied=false).
// Native gRPC uses only the first Hub dataset when multiple specs are written (hf-multi + extra_specs).
func applyDatasetMixToMergedToml(merged []byte, specs []nativeHFSpec, targetSamples int) ([]byte, bool, bool, error) {
	var clean []nativeHFSpec
	for _, s := range specs {
		p := strings.TrimSpace(s.Path)
		if p == "" {
			continue
		}
		clean = append(clean, nativeHFSpec{Path: p, DatasetConfig: strings.TrimSpace(s.DatasetConfig)})
	}
	if len(clean) == 0 {
		return merged, false, false, nil
	}
	tierNS := huggingFaceNumSamplesFromToml(merged)
	effective := effectiveNativeCascadeNumSamples(tierNS, targetSamples)
	if effective <= 0 {
		effective = tierNS
	}
	if effective <= 0 {
		effective = 512
	}
	const maxU32 = int64(4294967295)
	if effective > maxU32 {
		effective = maxU32
	}

	var b strings.Builder
	b.WriteString("[data]\nsource = \"hf_tabular\"\n")
	if len(clean) == 1 {
		b.WriteString("path = ")
		b.WriteString(quoteTomlString(clean[0].Path))
		b.WriteString("\n\n[huggingface]\n")
		if clean[0].DatasetConfig != "" {
			b.WriteString("dataset_config = ")
			b.WriteString(quoteTomlString(clean[0].DatasetConfig))
			b.WriteString("\n")
		}
		b.WriteString("split = \"train\"\n")
		b.WriteString(fmt.Sprintf("num_samples = %d\n", effective))
	} else {
		b.WriteString("path = \"qminiwasm/hf-multi\"\n\n[huggingface]\n")
		b.WriteString("split = \"train\"\n")
		b.WriteString(fmt.Sprintf("num_samples = %d\n", effective))
		for _, s := range clean {
			b.WriteString("\n[[huggingface.extra_specs]]\n")
			b.WriteString("path = ")
			b.WriteString(quoteTomlString(s.Path))
			b.WriteString("\n")
			if s.DatasetConfig != "" {
				b.WriteString("dataset_config = ")
				b.WriteString(quoteTomlString(s.DatasetConfig))
				b.WriteString("\n")
			}
		}
	}

	out, err := mergeTrainingTomlBytes(merged, b.String())
	if err != nil {
		return nil, false, false, err
	}
	return out, true, len(clean) > 1, nil
}

func mergeNativeCascadeTierToWorkfile(enclaveTier int, specs []nativeHFSpec, targetSamples int) (
	writtenRel string,
	overlayRel string,
	datasetMixApplied bool,
	nativeFirstSpecOnly bool,
	err error,
) {
	merged, overlayRel, err := buildNativeCascadeMergedBytes(enclaveTier)
	if err != nil {
		return "", "", false, false, err
	}
	if len(specs) > 0 {
		merged, datasetMixApplied, nativeFirstSpecOnly, err = applyDatasetMixToMergedToml(merged, specs, targetSamples)
		if err != nil {
			return "", overlayRel, false, false, err
		}
	}
	b := make([]byte, 4)
	if _, err := rand.Read(b); err != nil {
		return "", overlayRel, datasetMixApplied, nativeFirstSpecOnly, err
	}
	tag := hex.EncodeToString(b)
	name := fmt.Sprintf(".wui_native_cascade_t%d_%s.toml", enclaveTier, tag)
	rel := filepath.ToSlash(filepath.Join("configs", "training", name))
	abs := filepath.Join(repoRoot, "configs", "training", name)
	if err := os.WriteFile(abs, merged, 0o644); err != nil {
		return "", overlayRel, datasetMixApplied, nativeFirstSpecOnly, err
	}
	return rel, overlayRel, datasetMixApplied, nativeFirstSpecOnly, nil
}

func forceEnclaveTierInToml(raw []byte, tier int) ([]byte, error) {
	var root map[string]any
	if err := toml.Unmarshal(raw, &root); err != nil {
		return nil, fmt.Errorf("parse merged TOML: %w", err)
	}
	if root == nil {
		root = map[string]any{}
	}
	enc, ok := root["enclave"].(map[string]any)
	if !ok || enc == nil {
		enc = map[string]any{}
		root["enclave"] = enc
	}
	enc["enclave_tier"] = int64(tier)
	out, err := toml.Marshal(root)
	if err != nil {
		return nil, fmt.Errorf("marshal TOML: %w", err)
	}
	return out, nil
}

func mergeTrainingTomlFromFiles(baseAbs, overlayAbs string) ([]byte, error) {
	overlayRaw, err := os.ReadFile(overlayAbs)
	if err != nil {
		return nil, err
	}
	baseRaw, err := os.ReadFile(baseAbs)
	if err != nil {
		return nil, err
	}
	return mergeTrainingTomlBytes(baseRaw, string(overlayRaw))
}

type nativeCascadeApplyRequest struct {
	EnclaveTier   int            `json:"enclave_tier"`
	HFSpecs       []nativeHFSpec `json:"hf_specs"`
	TargetSamples int            `json:"target_samples"`
}

func handleNativeCascadeApply(w http.ResponseWriter, r *http.Request) {
	if !authOK(r) {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	raw, err := io.ReadAll(io.LimitReader(r.Body, 65536))
	if err != nil {
		jsonErr(w, http.StatusBadRequest, "read body: "+err.Error())
		return
	}
	var req nativeCascadeApplyRequest
	_ = json.Unmarshal(raw, &req)
	tier := 2
	if req.EnclaveTier >= 1 && req.EnclaveTier <= 5 {
		tier = req.EnclaveTier
	} else if q := strings.TrimSpace(r.URL.Query().Get("enclave_tier")); q != "" {
		var qt int
		if _, serr := fmt.Sscanf(q, "%d", &qt); serr == nil && qt >= 1 && qt <= 5 {
			tier = qt
		}
	}
	if tier < 1 || tier > 5 {
		jsonErr(w, http.StatusBadRequest, "enclave_tier must be 1–5")
		return
	}
	if req.TargetSamples < 0 {
		jsonErr(w, http.StatusBadRequest, "target_samples must be >= 0")
		return
	}
	written, overlay, mixApplied, firstOnly, werr := mergeNativeCascadeTierToWorkfile(tier, req.HFSpecs, req.TargetSamples)
	if werr != nil {
		jsonErr(w, http.StatusBadRequest, werr.Error())
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok":                         true,
		"config":                     written,
		"base_config":                cascadeNativeBaseRel,
		"tier_overlay":               overlay,
		"enclave_tier":               tier,
		"taxonomy_bundle":            strings.TrimSuffix(filepath.Base(overlay), ".toml"),
		"dataset_mix_applied":        mixApplied,
		"native_uses_first_spec_only": firstOnly,
	})
}
