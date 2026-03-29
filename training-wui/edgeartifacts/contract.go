package edgeartifacts

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
)

var hex64 = regexp.MustCompile(`^[0-9a-f]{64}$`)

const (
	BundleZipName   = "qminiwasm-edge-bundle.zip"
	BundleTpemName  = "qminiwasm-edge-bundle.tpem"
	EdgeSchemaName  = "edge_schema.json"
	ManifestName    = "artifact_manifest.json"
	KernelsWasmName = "qminiwasm-kernels.wasm"
	WeightsTpemName = "qminiwasm-weights.tpem"
)

// VerifyArtifactContract validates artifact_manifest.json against files on disk (Python parity).
func VerifyArtifactContract(artifactsDir string) error {
	base := filepath.Clean(artifactsDir)
	manifestPath := filepath.Join(base, ManifestName)
	raw, err := os.ReadFile(manifestPath)
	if err != nil {
		return fmt.Errorf("manifest: %w", err)
	}
	var manifest map[string]any
	if err := json.Unmarshal(raw, &manifest); err != nil {
		return fmt.Errorf("manifest json: %w", err)
	}
	if err := validateManifestShape(manifest); err != nil {
		return err
	}
	artifacts := manifest["artifacts"].(map[string]any)

	kernelsMeta := artifacts["kernels_wasm"].(map[string]any)
	kernelsPath := filepath.Join(base, str(kernelsMeta["path"]))
	kb, err := os.ReadFile(kernelsPath)
	if err != nil {
		return fmt.Errorf("kernels wasm: %w", err)
	}
	if err := checkBytesSHA(kernelsMeta, kb, "kernels_wasm", "bytes", "sha256"); err != nil {
		return err
	}

	tpemMeta := artifacts["weights_tpem"].(map[string]any)
	tpemPath := filepath.Join(base, str(tpemMeta["path"]))
	bundle, err := ReadTpemBundle(tpemPath)
	if err != nil {
		return fmt.Errorf("weights tpem: %w", err)
	}
	if err := checkTPEMMeta(tpemMeta, bundle); err != nil {
		return err
	}

	if esMeta, ok := artifacts["edge_schema"].(map[string]any); ok {
		esPath := filepath.Join(base, str(esMeta["path"]))
		esb, err := os.ReadFile(esPath)
		if err != nil {
			return fmt.Errorf("edge_schema: %w", err)
		}
		if err := checkBytesSHA(esMeta, esb, "edge_schema", "bytes", "sha256"); err != nil {
			return err
		}
	}

	var ebSHA string
	if ebMeta, ok := artifacts["edge_bundle"].(map[string]any); ok {
		ebPath := filepath.Join(base, str(ebMeta["path"]))
		ebb, err := os.ReadFile(ebPath)
		if err != nil {
			return fmt.Errorf("edge_bundle: %w", err)
		}
		if err := checkBytesSHA(ebMeta, ebb, "edge_bundle", "bytes", "sha256"); err != nil {
			return err
		}
		ebSHA = sha256Hex(ebb)
	}

	if ebtMeta, ok := artifacts["edge_bundle_tpem"].(map[string]any); ok {
		ebtPath := filepath.Join(base, str(ebtMeta["path"]))
		ebtb, err := os.ReadFile(ebtPath)
		if err != nil {
			return fmt.Errorf("edge_bundle_tpem: %w", err)
		}
		if err := checkBytesSHA(ebtMeta, ebtb, "edge_bundle_tpem", "bytes", "sha256"); err != nil {
			return err
		}
		if ebSHA != "" && sha256Hex(ebtb) != ebSHA {
			return fmt.Errorf("edge_bundle_tpem must match edge_bundle checksums")
		}
	}

	return nil
}

func str(v any) string {
	if v == nil {
		return ""
	}
	return fmt.Sprint(v)
}

func sha256Hex(b []byte) string {
	h := sha256.Sum256(b)
	return hex.EncodeToString(h[:])
}

func checkBytesSHA(meta map[string]any, data []byte, label, bytesKey, shaKey string) error {
	wantN, _ := toInt64(meta[bytesKey])
	if int64(len(data)) != wantN {
		return fmt.Errorf("%s byte mismatch: expected %d got %d", label, wantN, len(data))
	}
	got := sha256Hex(data)
	want := str(meta[shaKey])
	if got != want {
		return fmt.Errorf("%s sha256 mismatch", label)
	}
	return nil
}

func checkTPEMMeta(meta map[string]any, bundle *TpemBundle) error {
	wantN, _ := toInt64(meta["payload_bytes"])
	if int64(len(bundle.Payload)) != wantN {
		return fmt.Errorf("tpem payload byte mismatch: expected %d got %d", wantN, len(bundle.Payload))
	}
	wantSHA := str(meta["payload_sha256"])
	if sha256Hex(bundle.Payload) != wantSHA {
		return fmt.Errorf("tpem payload sha256 mismatch")
	}
	bfv, _ := toInt64(meta["bundle_format_version"])
	if int64(bundle.BundleFormatVersion) != bfv {
		return fmt.Errorf("tpem bundle_format_version mismatch")
	}
	pev, _ := toInt64(meta["pack_encoding_version"])
	if int64(bundle.PackEncodingVersion) != pev {
		return fmt.Errorf("tpem pack_encoding_version mismatch")
	}
	return nil
}

func toInt64(v any) (int64, bool) {
	switch x := v.(type) {
	case float64:
		return int64(x), true
	case int:
		return int64(x), true
	case int64:
		return x, true
	case json.Number:
		i, err := x.Int64()
		return i, err == nil
	default:
		return 0, false
	}
}

func validateManifestShape(manifest map[string]any) error {
	artifacts, ok := manifest["artifacts"].(map[string]any)
	if !ok {
		return fmt.Errorf("manifest.artifacts must be an object")
	}
	var errs []string
	for _, k := range []string{"kernels_wasm", "weights_tpem"} {
		if _, ok := artifacts[k].(map[string]any); !ok {
			errs = append(errs, fmt.Sprintf("manifest.artifacts.%s must be an object", k))
		}
	}
	if len(errs) > 0 {
		return fmt.Errorf("%s", errs[0])
	}
	kernels := artifacts["kernels_wasm"].(map[string]any)
	for _, k := range []string{"path", "sha256", "bytes"} {
		if _, ok := kernels[k]; !ok {
			return fmt.Errorf("manifest.artifacts.kernels_wasm.%s is required", k)
		}
	}
	weights := artifacts["weights_tpem"].(map[string]any)
	for _, k := range []string{"path", "payload_sha256", "payload_bytes", "bundle_format_version", "pack_encoding_version"} {
		if _, ok := weights[k]; !ok {
			return fmt.Errorf("manifest.artifacts.weights_tpem.%s is required", k)
		}
	}
	if es, ok := artifacts["edge_schema"].(map[string]any); ok {
		for _, k := range []string{"path", "sha256", "bytes"} {
			if _, ok := es[k]; !ok {
				return fmt.Errorf("manifest.artifacts.edge_schema.%s is required", k)
			}
		}
		if !hex64.MatchString(str(es["sha256"])) {
			return fmt.Errorf("manifest.artifacts.edge_schema.sha256 must be 64 lowercase hex chars")
		}
	}
	if eb, ok := artifacts["edge_bundle"].(map[string]any); ok {
		for _, k := range []string{"path", "sha256", "bytes"} {
			if _, ok := eb[k]; !ok {
				return fmt.Errorf("manifest.artifacts.edge_bundle.%s is required", k)
			}
		}
		if !hex64.MatchString(str(eb["sha256"])) {
			return fmt.Errorf("manifest.artifacts.edge_bundle.sha256 must be 64 lowercase hex chars")
		}
	}
	if ebt, ok := artifacts["edge_bundle_tpem"].(map[string]any); ok {
		for _, k := range []string{"path", "sha256", "bytes"} {
			if _, ok := ebt[k]; !ok {
				return fmt.Errorf("manifest.artifacts.edge_bundle_tpem.%s is required", k)
			}
		}
		if !hex64.MatchString(str(ebt["sha256"])) {
			return fmt.Errorf("manifest.artifacts.edge_bundle_tpem.sha256 must be 64 lowercase hex chars")
		}
	}
	if !hex64.MatchString(str(kernels["sha256"])) {
		return fmt.Errorf("manifest.artifacts.kernels_wasm.sha256 must be 64 lowercase hex chars")
	}
	if !hex64.MatchString(str(weights["payload_sha256"])) {
		return fmt.Errorf("manifest.artifacts.weights_tpem.payload_sha256 must be 64 lowercase hex chars")
	}
	return nil
}
