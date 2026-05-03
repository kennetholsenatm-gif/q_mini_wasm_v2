package main

import (
	"archive/zip"
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

const embeddedAssetFooterMagic = "QMINIWASM_WUI_ASSET_FOOTER_V1"

type EmbeddedAssetEntry struct {
	Path   string `json:"path"`
	Size   int    `json:"size"`
	SHA256 string `json:"sha256"`
}

type EmbeddedAssetManifest struct {
	FormatVersion        string               `json:"format_version"`
	BundleSchema         string               `json:"bundle_schema"`
	ShellContractVersion string               `json:"shell_contract_version"`
	NativeRuntimeAssets  []string             `json:"native_runtime_assets,omitempty"`
	Entries              []EmbeddedAssetEntry `json:"entries"`
}

func hashBytes(data []byte) string {
	sum := sha256.Sum256(data)
	return hex.EncodeToString(sum[:])
}

func shouldPackageWUIAsset(rel string) bool {
	if strings.HasPrefix(rel, "../") {
		return false
	}
	if rel == "wui/js/websocket-bridge.js" || rel == "wui/js/wui-mcp-bridge.js" {
		return false
	}
	if strings.Contains(rel, "/.") {
		return false
	}
	return strings.HasPrefix(rel, "wui/")
}

func collectAssets(projectRoot string) (map[string][]byte, []string, error) {
	assets := map[string][]byte{}
	nativeAssets := []string{}

	// Collect WUI files
	wuiRoot := filepath.Join(projectRoot, "wui")
	err := filepath.WalkDir(wuiRoot, func(current string, d os.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if d.IsDir() {
			return nil
		}

		rel, err := filepath.Rel(projectRoot, current)
		if err != nil {
			return err
		}
		rel = strings.ReplaceAll(rel, "\\", "/")

		if !shouldPackageWUIAsset(rel) {
			return nil
		}

		content, err := os.ReadFile(current)
		if err != nil {
			return err
		}
		assets[rel] = content
		return nil
	})
	if err != nil {
		return nil, nil, err
	}

	// Extra files
	extraFiles := []string{
		"runtime/desktop_shell_contract.json",
		"runtime/desktop_shell_bootstrap.js",
		"config/system.toml",
	}

	for _, rel := range extraFiles {
		abs := filepath.Join(projectRoot, rel)
		if _, err := os.Stat(abs); os.IsNotExist(err) {
			continue
		}
		content, err := os.ReadFile(abs)
		if err != nil {
			continue
		}
		assets[rel] = content
	}

	// Native runtime assets
	nativePaths := []string{
		filepath.Join("q_mini_wasm_v2", "build_sycl", "Release", "q_mini_wasm_v2_trainer.exe"),
		filepath.Join("q_mini_wasm_v2", "build_sycl", "q_mini_wasm_v2_trainer.exe"),
	}

	for _, p := range nativePaths {
		abs := filepath.Join(projectRoot, p)
		if _, err := os.Stat(abs); os.IsNotExist(err) {
			continue
		}
		content, err := os.ReadFile(abs)
		if err != nil {
			continue
		}
		base := filepath.Base(p)
		rel := "runtime/native/" + base
		assets[rel] = content
		nativeAssets = append(nativeAssets, rel)
	}

	sort.Strings(nativeAssets)
	return assets, nativeAssets, nil
}

func main() {
	projectRoot := `c:\GitHub\q_mini_wasm_v2`
	exePath := filepath.Join(projectRoot, "qminiwasm.exe")

	// Check if exe exists
	if _, err := os.Stat(exePath); os.IsNotExist(err) {
		fmt.Fprintf(os.Stderr, "Executable not found: %s\n", exePath)
		os.Exit(1)
	}

	assets, nativeAssets, err := collectAssets(projectRoot)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error collecting assets: %v\n", err)
		os.Exit(1)
	}

	keys := make([]string, 0, len(assets))
	for k := range assets {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	entries := make([]EmbeddedAssetEntry, 0, len(keys))
	for _, k := range keys {
		content := assets[k]
		entries = append(entries, EmbeddedAssetEntry{
			Path:   k,
			Size:   len(content),
			SHA256: hashBytes(content),
		})
	}

	manifest := EmbeddedAssetManifest{
		FormatVersion:        "1",
		BundleSchema:         "qmini.desktop.asset_bundle.v1",
		ShellContractVersion: "qmini.desktop.mcp_host.v1",
		NativeRuntimeAssets:  nativeAssets,
		Entries:              entries,
	}

	manifestJSON, err := json.MarshalIndent(manifest, "", "  ")
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error marshaling manifest: %v\n", err)
		os.Exit(1)
	}
	manifestJSON = append(manifestJSON, '\n')

	// Add manifest to assets
	assets["runtime/embedded_asset_manifest.json"] = manifestJSON
	keys = append(keys, "runtime/embedded_asset_manifest.json")
	sort.Strings(keys)

	// Build zip bundle
	bundleBuf := bytes.NewBuffer(nil)
	zipWriter := zip.NewWriter(bundleBuf)

	zipEpochTime := time.Unix(0, 0).UTC()

	for _, k := range keys {
		hdr := &zip.FileHeader{
			Name:   k,
			Method: zip.Deflate,
		}
		hdr.SetMode(0644)
		hdr.SetModTime(zipEpochTime)

		w, err := zipWriter.CreateHeader(hdr)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error creating zip header: %v\n", err)
			os.Exit(1)
		}
		if _, err := w.Write(assets[k]); err != nil {
			fmt.Fprintf(os.Stderr, "Error writing to zip: %v\n", err)
			os.Exit(1)
		}
	}

	if err := zipWriter.Close(); err != nil {
		fmt.Fprintf(os.Stderr, "Error closing zip: %v\n", err)
		os.Exit(1)
	}

	bundle := bundleBuf.Bytes()
	bundleHash := hashBytes(bundle)

	fmt.Printf("Bundle: %d bytes, %d files\n", len(bundle), len(keys))
	fmt.Printf("Bundle hash: %s\n", bundleHash)

	// Print index.html info
	for _, e := range entries {
		if e.Path == "wui/index.html" {
			fmt.Printf("index.html: %d bytes\n", e.Size)
		}
		if e.Path == "wui/training.html" {
			fmt.Printf("training.html: %d bytes\n", e.Size)
		}
	}

	// Append to executable
	f, err := os.OpenFile(exePath, os.O_WRONLY|os.O_APPEND, 0)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error opening exe: %v\n", err)
		os.Exit(1)
	}
	defer f.Close()

	if _, err := f.Write(bundle); err != nil {
		fmt.Fprintf(os.Stderr, "Error writing bundle: %v\n", err)
		os.Exit(1)
	}

	// Write footer
	footer := bytes.NewBuffer(nil)
	footer.WriteString(embeddedAssetFooterMagic)
	sizeBytes := make([]byte, 8)
	binary.LittleEndian.PutUint64(sizeBytes, uint64(len(bundle)))
	footer.Write(sizeBytes)

	hashRaw, _ := hex.DecodeString(bundleHash)
	footer.Write(hashRaw)

	if _, err := f.Write(footer.Bytes()); err != nil {
		fmt.Fprintf(os.Stderr, "Error writing footer: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("\nSuccessfully appended bundle to %s\n", exePath)
	fmt.Printf("Final size: %d bytes\n", getFileSize(exePath))
}

func getFileSize(path string) int64 {
	stat, err := os.Stat(path)
	if err != nil {
		return -1
	}
	return stat.Size()
}
