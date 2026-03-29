package edgeartifacts

import (
	"archive/zip"
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

// BuildOptions drives a full edge-artifacts directory write (Python build script parity).
type BuildOptions struct {
	RepoRoot       string
	OutDir         string
	Tier           int
	WasmOptLevel   string
	CheckpointPath string // empty => deterministic synthetic payload
	PythonExe      string // used when CheckpointPath is set
}

// SyntheticPackedPayload returns the same 635-trit packed blob as scripts/build_tpem_wasm_artifacts.py (no checkpoint).
func SyntheticPackedPayload() ([]byte, error) {
	block := []int{-1, 0, 1, 1, 0}
	trits := make([]int, 0, 635)
	for i := 0; i < 127; i++ {
		trits = append(trits, block...)
	}
	return PackTernaryList(trits)
}

// Build writes wasm, tpem, schema, zip, bundle.tpem copy, manifest; runs VerifyArtifactContract.
func Build(opts BuildOptions) (strings.Builder, error) {
	var log strings.Builder
	repo := filepath.Clean(opts.RepoRoot)
	out := filepath.Clean(opts.OutDir)
	if opts.Tier < 1 || opts.Tier > 5 {
		return log, fmt.Errorf("tier must be 1–5, got %d", opts.Tier)
	}
	if err := os.MkdirAll(out, 0o755); err != nil {
		return log, err
	}

	var payload []byte
	var err error
	if strings.TrimSpace(opts.CheckpointPath) == "" {
		payload, err = SyntheticPackedPayload()
		if err != nil {
			return log, err
		}
		log.WriteString("using deterministic synthetic packed weights (no checkpoint)\n")
	} else {
		py := strings.TrimSpace(opts.PythonExe)
		if py == "" {
			py = "python"
		}
		payload, err = ExtractCheckpointPayload(repo, py, filepath.Clean(opts.CheckpointPath))
		if err != nil {
			return log, err
		}
		log.WriteString("packed weights from checkpoint via scripts/checkpoint_tpem_payload.py\n")
	}

	var wasmBytes []byte
	wasmBytes, err = CompileTritKernelsWasm(repo)
	if err != nil {
		return log, err
	}
	wasmBytes, warn := MaybeWasmOptTier1(wasmBytes, opts.Tier, opts.WasmOptLevel)
	if warn != "" {
		log.WriteString(warn)
		log.WriteByte('\n')
	}
	wasmPath := filepath.Join(out, KernelsWasmName)
	if err := os.WriteFile(wasmPath, wasmBytes, 0o644); err != nil {
		return log, err
	}
	wasmDigest := sha256Hex(wasmBytes)

	tpemPath := filepath.Join(out, WeightsTpemName)
	if err := WriteTpemBundle(tpemPath, payload, PACK_ENCODING_VERSION); err != nil {
		return log, err
	}
	payloadDigest := sha256Hex(payload)

	var schemaBytes []byte
	schemaBytes, err = EdgeSchemaJSON(opts.Tier)
	if err != nil {
		return log, err
	}
	schemaPath := filepath.Join(out, EdgeSchemaName)
	if err := os.WriteFile(schemaPath, schemaBytes, 0o644); err != nil {
		return log, err
	}
	schemaDigest := sha256Hex(schemaBytes)

	var zipDigest string
	var zipLen int
	zipDigest, zipLen, err = writePayloadZip(out, []zipEntry{
		{path: wasmPath, arc: KernelsWasmName},
		{path: tpemPath, arc: WeightsTpemName},
		{path: schemaPath, arc: EdgeSchemaName},
	})
	if err != nil {
		return log, err
	}
	bundleTpemPath := filepath.Join(out, BundleTpemName)
	srcZip := filepath.Join(out, BundleZipName)
	if err := copyFile(srcZip, bundleTpemPath); err != nil {
		return log, err
	}

	manifest := map[string]any{
		"artifacts": map[string]any{
			"kernels_wasm": map[string]any{
				"path":   KernelsWasmName,
				"sha256": wasmDigest,
				"bytes":  len(wasmBytes),
			},
			"weights_tpem": map[string]any{
				"path":                  WeightsTpemName,
				"payload_sha256":        payloadDigest,
				"payload_bytes":         len(payload),
				"bundle_format_version": int(BundleFormatVersion),
				"pack_encoding_version": PACK_ENCODING_VERSION,
			},
			"edge_schema": map[string]any{
				"path":   EdgeSchemaName,
				"sha256": schemaDigest,
				"bytes":  len(schemaBytes),
			},
			"edge_bundle": map[string]any{
				"path":   BundleZipName,
				"sha256": zipDigest,
				"bytes":  zipLen,
			},
			"edge_bundle_tpem": map[string]any{
				"path":   BundleTpemName,
				"sha256": zipDigest,
				"bytes":  zipLen,
			},
		},
	}
	manifestPath := filepath.Join(out, ManifestName)
	var mb []byte
	mb, err = json.MarshalIndent(manifest, "", "  ")
	if err != nil {
		return log, err
	}
	mb = append(mb, '\n')
	if err := os.WriteFile(manifestPath, mb, 0o644); err != nil {
		return log, err
	}

	if err := VerifyArtifactContract(out); err != nil {
		return log, err
	}

	fmt.Fprintf(&log, "Wrote %s (%d bytes, sha256=%s)\n", wasmPath, len(wasmBytes), wasmDigest)
	fmt.Fprintf(&log, "Wrote %s (payload %d bytes, sha256=%s)\n", tpemPath, len(payload), payloadDigest)
	fmt.Fprintf(&log, "Wrote %s\n", schemaPath)
	fmt.Fprintf(&log, "Wrote %s\n", manifestPath)
	fmt.Fprintf(&log, "Wrote %s (sha256=%s)\n", filepath.Join(out, BundleZipName), zipDigest)
	fmt.Fprintf(&log, "Wrote %s (same payload as zip, sha256=%s)\n", bundleTpemPath, zipDigest)
	return log, nil
}

type zipEntry struct {
	path, arc string
}

func writePayloadZip(outDir string, entries []zipEntry) (sha256hex string, n int, err error) {
	zpath := filepath.Join(outDir, BundleZipName)
	var buf bytes.Buffer
	zw := zip.NewWriter(&buf)
	for _, e := range entries {
		data, err := os.ReadFile(e.path)
		if err != nil {
			_ = zw.Close()
			return "", 0, err
		}
		w, err := zw.Create(e.arc)
		if err != nil {
			_ = zw.Close()
			return "", 0, err
		}
		if _, err := w.Write(data); err != nil {
			_ = zw.Close()
			return "", 0, err
		}
	}
	if err := zw.Close(); err != nil {
		return "", 0, err
	}
	raw := buf.Bytes()
	sum := sha256.Sum256(raw)
	if err := os.WriteFile(zpath, raw, 0o644); err != nil {
		return "", 0, err
	}
	return hex.EncodeToString(sum[:]), len(raw), nil
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()
	_, err = io.Copy(out, in)
	return err
}
