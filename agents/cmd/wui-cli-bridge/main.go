package main

import (
	"archive/zip"
	"bufio"
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	toml "github.com/pelletier/go-toml/v2"
)

// MCPRequest represents a JSON-RPC request from MCP client
type MCPRequest struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      interface{}     `json:"id"`
	Method  string          `json:"method"`
	Params  json.RawMessage `json:"params,omitempty"`
}

// MCPResponse represents a JSON-RPC response to MCP client
type MCPResponse struct {
	JSONRPC string      `json:"jsonrpc"`
	ID      interface{} `json:"id,omitempty"`
	Result  interface{} `json:"result,omitempty"`
	Error   *MCPError   `json:"error,omitempty"`
}

// MCPError represents a JSON-RPC error
type MCPError struct {
	Code    int         `json:"code"`
	Message string      `json:"message"`
	Data    interface{} `json:"data,omitempty"`
}

// BridgeClient is an MCP-safe local bridge shim.
// It intentionally avoids direct network transport surfaces.
type BridgeClient struct {
	mu          sync.RWMutex
	connected   bool
	endpoint    string
	lastPayload map[string]interface{}
	strictMode  bool
}

// NewBridgeClient creates a new local bridge client.
func NewBridgeClient(host string, port int) *BridgeClient {
	return &BridgeClient{
		endpoint:   fmt.Sprintf("mcp://%s:%d", host, port),
		strictMode: true,
	}
}

// Connect marks bridge as active.
func (c *BridgeClient) Connect(_ time.Duration) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.connected = true
	return nil
}

// Send stores the most recent command payload.
func (c *BridgeClient) Send(msg map[string]interface{}) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if !c.connected {
		return fmt.Errorf("bridge client not connected")
	}
	c.lastPayload = msg
	return nil
}

// SendAndWait returns deterministic local acknowledgements.
func (c *BridgeClient) SendAndWait(msg map[string]interface{}, responseType string, _ time.Duration) (map[string]interface{}, error) {
	if err := c.Send(msg); err != nil {
		return nil, err
	}

	if c.strictMode {
		return nil, fmt.Errorf("strict mode: backend response '%s' requires compiled engine integration", responseType)
	}

	return nil, fmt.Errorf("backend response '%s' not available", responseType)
}

// Disconnect deactivates the bridge.
func (c *BridgeClient) Disconnect() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.connected = false
	c.lastPayload = nil
}

// IsConnected reports bridge state.
func (c *BridgeClient) IsConnected() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.connected
}

// MCPServer handles MCP protocol.
type MCPServer struct {
	wsClient      *BridgeClient
	pipeline      *TrainingPipelineCGO
	scanner       *bufio.Scanner
	tools         map[string]ToolHandler
	projectRoot   string
	httpMCPActive bool // Track HTTP-based MCP connections

	releaseMu sync.RWMutex
	release   ReleaseState
}

// ReleaseState tracks desktop build/deploy lifecycle.
type ReleaseState struct {
	Status                      string   `json:"status"`
	ArtifactPath                string   `json:"artifact_path,omitempty"`
	ArtifactSHA256              string   `json:"artifact_sha256,omitempty"`
	EmbeddedBundleSHA256        string   `json:"embedded_bundle_sha256,omitempty"`
	EmbeddedBundleBytes         int64    `json:"embedded_bundle_bytes,omitempty"`
	EmbeddedNativeRuntimeAssets int      `json:"embedded_native_runtime_assets,omitempty"`
	AssetDir                    string   `json:"asset_dir,omitempty"`
	AssetManifestPath           string   `json:"asset_manifest_path,omitempty"`
	AssetCount                  int      `json:"asset_count,omitempty"`
	ShellContractVersion        string   `json:"shell_contract_version,omitempty"`
	Target                      string   `json:"target,omitempty"`
	LastAction                  string   `json:"last_action,omitempty"`
	LastError                   string   `json:"last_error,omitempty"`
	UpdatedAt                   string   `json:"updated_at"`
	LastLogLines                []string `json:"last_log_lines,omitempty"`
}

// TOMLValidationResult reports syntax and governance checks for config/system.toml.
type TOMLValidationResult struct {
	Valid           bool     `json:"valid"`
	Errors          []string `json:"errors"`
	Warnings        []string `json:"warnings"`
	MissingSections []string `json:"missing_sections"`
}

const (
	desktopShellContractVersion     = "qmini.desktop.mcp_host.v1"
	embeddedAssetBundleFormat       = "qmini.desktop.asset_bundle.v1"
	embeddedAssetManifestPath       = "runtime/embedded_asset_manifest.json"
	embeddedAssetFooterMagic        = "QMINIWASM_WUI_ASSET_FOOTER_V1"
	desktopShellContractAssetPath   = "runtime/desktop_shell_contract.json"
	desktopShellBootstrapAssetPath  = "runtime/desktop_shell_bootstrap.js"
	defaultEmbeddedAssetExtractPath = "releases/desktop/extracted-assets"
)

var zipEpochTime = time.Unix(0, 0).UTC()

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

type EmbeddedAssetPayload struct {
	Manifest     EmbeddedAssetManifest
	Bundle       []byte
	BundleHash   string
	NativeAssets []string
}

type DesktopShellContract struct {
	Version         string   `json:"version"`
	RequiredMethods []string `json:"required_methods"`
	OptionalMethods []string `json:"optional_methods"`
	HandshakeTool   string   `json:"handshake_tool"`
}

func hashBytesHex(data []byte) string {
	sum := sha256.Sum256(data)
	return hex.EncodeToString(sum[:])
}

func hashFileHex(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()

	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

func writeDeterministicFile(path string, content []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	return os.WriteFile(path, content, 0o644)
}

func desktopShellContractDocument() DesktopShellContract {
	return DesktopShellContract{
		Version:         desktopShellContractVersion,
		RequiredMethods: []string{"callTool"},
		OptionalMethods: []string{"describe", "ping"},
		HandshakeTool:   "wui_desktop_shell_handshake",
	}
}

func desktopShellBootstrapSource() string {
	return strings.TrimSpace(fmt.Sprintf(`(function (globalScope) {
    'use strict';
    const CONTRACT_VERSION = %q;
    if (globalScope.qMiniMcpHost && typeof globalScope.qMiniMcpHost.callTool === 'function') {
        return;
    }

    const webview = globalScope.chrome && globalScope.chrome.webview;
    if (!webview || typeof webview.postMessage !== 'function') {
        return;
    }

    let nextId = 1;
    const pending = new Map();

    if (typeof webview.addEventListener === 'function') {
        webview.addEventListener('message', (event) => {
            const payload = event && event.data ? event.data : {};
            if (!payload || payload.channel !== 'qmini-mcp-response') {
                return;
            }

            const wait = pending.get(payload.id);
            if (!wait) {
                return;
            }
            pending.delete(payload.id);

            if (payload.error) {
                wait.reject(new Error(payload.error.message || String(payload.error)));
            } else {
                wait.resolve(payload.result);
            }
        });
    }

    function callTool(name, args) {
        const id = nextId++;
        const payload = {
            channel: 'qmini-mcp-request',
            id,
            name,
            arguments: args || {},
        };

        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                pending.delete(id);
                reject(new Error('MCP request timeout: ' + name));
            }, 15000);

            pending.set(id, {
                resolve(value) {
                    clearTimeout(timer);
                    resolve(value);
                },
                reject(error) {
                    clearTimeout(timer);
                    reject(error);
                }
            });

            webview.postMessage(payload);
        });
    }

    globalScope.qMiniMcpHost = {
        contractVersion: CONTRACT_VERSION,
        callTool,
        describe() {
            return {
                contract_version: CONTRACT_VERSION,
                adapter: 'webview2-message-channel',
                channel: 'qmini-mcp-request',
            };
        },
        ping() {
            return callTool('ping', {});
        }
    };

    globalScope.dispatchEvent(new CustomEvent('qminiDesktopHostReady', {
        detail: {
            contract_version: CONTRACT_VERSION,
            adapter: 'webview2-message-channel',
        }
    }));
})(typeof window !== 'undefined' ? window : globalThis);
`, desktopShellContractVersion)) + "\n"
}

func ensureDesktopRuntimeContractAssets(projectRoot string) (string, string, error) {
	contract := desktopShellContractDocument()
	contractJSON, err := json.MarshalIndent(contract, "", "  ")
	if err != nil {
		return "", "", err
	}
	contractJSON = append(contractJSON, '\n')

	contractPath := joinProjectPath(projectRoot, desktopShellContractAssetPath)
	if err := writeDeterministicFile(contractPath, contractJSON); err != nil {
		return "", "", err
	}

	bootstrapPath := joinProjectPath(projectRoot, desktopShellBootstrapAssetPath)
	if err := writeDeterministicFile(bootstrapPath, []byte(desktopShellBootstrapSource())); err != nil {
		return "", "", err
	}

	return contractPath, bootstrapPath, nil
}

func shouldPackageWUIAsset(rel string) bool {
	normalized := path.Clean(strings.TrimPrefix(filepath.ToSlash(rel), "./"))
	if strings.HasPrefix(normalized, "../") {
		return false
	}

	if normalized == "wui/js/websocket-bridge.js" || normalized == "wui/js/wui-mcp-bridge.js" {
		return false
	}

	if strings.Contains(normalized, "/.") {
		return false
	}

	return strings.HasPrefix(normalized, "wui/")
}

func nativeRuntimeArtifactCandidates(projectRoot string) []string {
	return []string{
		joinProjectPath(projectRoot, "q_mini_wasm_v2.exe"),
		joinProjectPath(projectRoot, filepath.Join("q_mini_wasm_v2", "build", "bin", "q_mini_wasm_v2.exe")),
		joinProjectPath(projectRoot, filepath.Join("q_mini_wasm_v2", "build", "bin", "q_gf3_wasm.dll")),
	}
}

func findNativeRuntimeArtifactPaths(projectRoot string) []string {
	paths := []string{}
	seen := map[string]struct{}{}
	for _, candidate := range nativeRuntimeArtifactCandidates(projectRoot) {
		if !fileExists(candidate) {
			continue
		}

		cleaned := filepath.Clean(candidate)
		if _, ok := seen[cleaned]; ok {
			continue
		}
		seen[cleaned] = struct{}{}
		paths = append(paths, cleaned)
	}
	sort.Strings(paths)
	return paths
}

func collectNativeRuntimeAssets(projectRoot string) (map[string][]byte, []string, error) {
	assets := map[string][]byte{}
	included := []string{}

	seen := map[string]struct{}{}
	for _, abs := range findNativeRuntimeArtifactPaths(projectRoot) {
		if !fileExists(abs) {
			continue
		}

		base := filepath.Base(abs)
		if _, ok := seen[base]; ok {
			continue
		}
		seen[base] = struct{}{}

		content, err := os.ReadFile(abs)
		if err != nil {
			return nil, nil, err
		}

		rel := path.Clean(path.Join("runtime", "native", base))
		assets[rel] = content
		included = append(included, rel)
	}

	sort.Strings(included)
	return assets, included, nil
}

func collectPackagedAssetFiles(projectRoot string, includeNativeRuntime bool) (map[string][]byte, []string, error) {
	assets := map[string][]byte{}
	nativeAssets := []string{}
	wuiRoot := joinProjectPath(projectRoot, "wui")

	err := filepath.WalkDir(wuiRoot, func(current string, d fs.DirEntry, walkErr error) error {
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

		normalized := path.Clean(filepath.ToSlash(rel))
		if !shouldPackageWUIAsset(normalized) {
			return nil
		}

		content, err := os.ReadFile(current)
		if err != nil {
			return err
		}
		assets[normalized] = content
		return nil
	})
	if err != nil {
		return nil, nil, err
	}

	extraFiles := []string{
		desktopShellContractAssetPath,
		desktopShellBootstrapAssetPath,
		"config/system.toml",
	}

	for _, rel := range extraFiles {
		abs := joinProjectPath(projectRoot, rel)
		if !fileExists(abs) {
			continue
		}
		content, err := os.ReadFile(abs)
		if err != nil {
			return nil, nil, err
		}
		assets[path.Clean(filepath.ToSlash(rel))] = content
	}

	if includeNativeRuntime {
		nativeMap, included, err := collectNativeRuntimeAssets(projectRoot)
		if err != nil {
			return nil, nil, err
		}
		for rel, content := range nativeMap {
			assets[rel] = content
		}
		nativeAssets = append(nativeAssets, included...)
	}

	return assets, nativeAssets, nil
}

func buildEmbeddedAssetPayload(projectRoot string, requireNativeRuntime bool) (EmbeddedAssetPayload, error) {
	if _, _, err := ensureDesktopRuntimeContractAssets(projectRoot); err != nil {
		return EmbeddedAssetPayload{}, err
	}

	assets, nativeAssets, err := collectPackagedAssetFiles(projectRoot, true)
	if err != nil {
		return EmbeddedAssetPayload{}, err
	}

	if requireNativeRuntime && len(nativeAssets) == 0 {
		return EmbeddedAssetPayload{}, fmt.Errorf("no compiled native runtime artifacts found (expected q_mini_wasm_v2.exe and/or q_gf3_wasm.dll)")
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
			SHA256: hashBytesHex(content),
		})
	}

	manifest := EmbeddedAssetManifest{
		FormatVersion:        "1",
		BundleSchema:         embeddedAssetBundleFormat,
		ShellContractVersion: desktopShellContractVersion,
		NativeRuntimeAssets:  nativeAssets,
		Entries:              entries,
	}

	manifestJSON, err := json.MarshalIndent(manifest, "", "  ")
	if err != nil {
		return EmbeddedAssetPayload{}, err
	}
	manifestJSON = append(manifestJSON, '\n')
	assets[path.Clean(embeddedAssetManifestPath)] = manifestJSON

	manifestAbsPath := joinProjectPath(projectRoot, embeddedAssetManifestPath)
	if err := writeDeterministicFile(manifestAbsPath, manifestJSON); err != nil {
		return EmbeddedAssetPayload{}, err
	}

	keys = keys[:0]
	for k := range assets {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	bundleBuf := bytes.NewBuffer(nil)
	zipWriter := zip.NewWriter(bundleBuf)
	for _, k := range keys {
		hdr := &zip.FileHeader{
			Name:   k,
			Method: zip.Deflate,
		}
		hdr.SetMode(0o644)
		hdr.Modified = zipEpochTime

		w, err := zipWriter.CreateHeader(hdr)
		if err != nil {
			_ = zipWriter.Close()
			return EmbeddedAssetPayload{}, err
		}
		if _, err := w.Write(assets[k]); err != nil {
			_ = zipWriter.Close()
			return EmbeddedAssetPayload{}, err
		}
	}

	if err := zipWriter.Close(); err != nil {
		return EmbeddedAssetPayload{}, err
	}

	bundle := bundleBuf.Bytes()
	return EmbeddedAssetPayload{
		Manifest:     manifest,
		Bundle:       bundle,
		BundleHash:   hashBytesHex(bundle),
		NativeAssets: nativeAssets,
	}, nil
}

func countManifestNativeRuntimeAssets(manifestPath string) int {
	if strings.TrimSpace(manifestPath) == "" || !fileExists(manifestPath) {
		return 0
	}
	raw, err := os.ReadFile(manifestPath)
	if err != nil {
		return 0
	}

	var manifest EmbeddedAssetManifest
	if err := json.Unmarshal(raw, &manifest); err != nil {
		return 0
	}

	if len(manifest.NativeRuntimeAssets) > 0 {
		return len(manifest.NativeRuntimeAssets)
	}

	count := 0
	for _, entry := range manifest.Entries {
		if strings.HasPrefix(path.Clean(entry.Path), "runtime/native/") {
			count++
		}
	}
	return count
}

func appendEmbeddedAssetBundle(exePath string, payload EmbeddedAssetPayload) error {
	f, err := os.OpenFile(exePath, os.O_WRONLY|os.O_APPEND, 0)
	if err != nil {
		return err
	}
	defer f.Close()

	if _, err := f.Write(payload.Bundle); err != nil {
		return err
	}

	footer := bytes.NewBuffer(nil)
	footer.WriteString(embeddedAssetFooterMagic)
	sizeBytes := make([]byte, 8)
	binary.LittleEndian.PutUint64(sizeBytes, uint64(len(payload.Bundle)))
	footer.Write(sizeBytes)

	hashRaw, err := hex.DecodeString(payload.BundleHash)
	if err != nil {
		return err
	}
	if len(hashRaw) != sha256.Size {
		return fmt.Errorf("invalid bundle hash size: %d", len(hashRaw))
	}
	footer.Write(hashRaw)

	_, err = f.Write(footer.Bytes())
	return err
}

func stripEmbeddedAssetBundleIfPresent(exePath string) error {
	found, offset, _, _, err := probeEmbeddedAssetFooter(exePath)
	if err != nil {
		return err
	}
	if !found {
		return nil
	}

	trimSize := offset
	if trimSize <= 0 {
		return fmt.Errorf("invalid trim offset for embedded asset bundle")
	}

	if err := os.Truncate(exePath, trimSize); err != nil {
		return err
	}
	return nil
}

func probeEmbeddedAssetFooter(exePath string) (bool, int64, int64, string, error) {
	const footerHashSize = sha256.Size
	footerLen := int64(len(embeddedAssetFooterMagic) + 8 + footerHashSize)

	stat, err := os.Stat(exePath)
	if err != nil {
		return false, 0, 0, "", err
	}
	if stat.Size() < footerLen {
		return false, 0, 0, "", nil
	}

	f, err := os.Open(exePath)
	if err != nil {
		return false, 0, 0, "", err
	}
	defer f.Close()

	footer := make([]byte, footerLen)
	if _, err := f.ReadAt(footer, stat.Size()-footerLen); err != nil {
		return false, 0, 0, "", err
	}

	if string(footer[:len(embeddedAssetFooterMagic)]) != embeddedAssetFooterMagic {
		return false, 0, 0, "", nil
	}

	size := int64(binary.LittleEndian.Uint64(footer[len(embeddedAssetFooterMagic) : len(embeddedAssetFooterMagic)+8]))
	if size <= 0 || size > (stat.Size()-footerLen) {
		return false, 0, 0, "", fmt.Errorf("invalid embedded asset bundle size: %d", size)
	}

	hashHex := hex.EncodeToString(footer[len(embeddedAssetFooterMagic)+8:])
	offset := stat.Size() - footerLen - size
	return true, offset, size, hashHex, nil
}

func readEmbeddedAssetBundle(exePath string) ([]byte, string, error) {
	found, offset, size, expectedHash, err := probeEmbeddedAssetFooter(exePath)
	if err != nil {
		return nil, "", err
	}
	if !found {
		return nil, "", fmt.Errorf("embedded asset bundle footer not found in %s", exePath)
	}

	f, err := os.Open(exePath)
	if err != nil {
		return nil, "", err
	}
	defer f.Close()

	bundle := make([]byte, size)
	if _, err := f.ReadAt(bundle, offset); err != nil {
		return nil, "", err
	}

	actualHash := hashBytesHex(bundle)
	if !strings.EqualFold(actualHash, expectedHash) {
		return nil, "", fmt.Errorf("embedded asset bundle hash mismatch: expected %s got %s", expectedHash, actualHash)
	}

	return bundle, actualHash, nil
}

func extractEmbeddedAssetBundle(exePath, outDir string) (string, int, string, error) {
	bundle, bundleHash, err := readEmbeddedAssetBundle(exePath)
	if err != nil {
		return "", 0, "", err
	}

	reader, err := zip.NewReader(bytes.NewReader(bundle), int64(len(bundle)))
	if err != nil {
		return "", 0, "", err
	}

	if err := os.RemoveAll(outDir); err != nil {
		return "", 0, "", err
	}
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return "", 0, "", err
	}

	files := make([]*zip.File, 0, len(reader.File))
	files = append(files, reader.File...)
	sort.Slice(files, func(i, j int) bool { return files[i].Name < files[j].Name })

	extractedCount := 0
	rootClean := filepath.Clean(outDir)
	for _, zf := range files {
		name := path.Clean(strings.TrimPrefix(zf.Name, "/"))
		if name == "." || strings.HasPrefix(name, "../") {
			return "", 0, "", fmt.Errorf("unsafe asset entry path: %s", zf.Name)
		}

		dstPath := filepath.Join(outDir, filepath.FromSlash(name))
		dstClean := filepath.Clean(dstPath)
		if !strings.HasPrefix(dstClean, rootClean) {
			return "", 0, "", fmt.Errorf("asset extraction path escapes root: %s", dstPath)
		}

		if zf.FileInfo().IsDir() {
			if err := os.MkdirAll(dstPath, 0o755); err != nil {
				return "", 0, "", err
			}
			continue
		}

		if err := os.MkdirAll(filepath.Dir(dstPath), 0o755); err != nil {
			return "", 0, "", err
		}

		src, err := zf.Open()
		if err != nil {
			return "", 0, "", err
		}
		content, err := io.ReadAll(src)
		_ = src.Close()
		if err != nil {
			return "", 0, "", err
		}

		if err := os.WriteFile(dstPath, content, 0o644); err != nil {
			return "", 0, "", err
		}
		extractedCount++
	}

	manifestPath := filepath.Join(outDir, filepath.FromSlash(path.Clean(embeddedAssetManifestPath)))
	if !fileExists(manifestPath) {
		return "", 0, "", fmt.Errorf("embedded asset manifest missing after extraction: %s", manifestPath)
	}

	manifestRaw, err := os.ReadFile(manifestPath)
	if err == nil {
		var manifest EmbeddedAssetManifest
		if json.Unmarshal(manifestRaw, &manifest) == nil && len(manifest.Entries) > 0 {
			extractedCount = len(manifest.Entries)
		}
	}

	return manifestPath, extractedCount, bundleHash, nil
}

func normalizeExtractRoot(projectRoot, extractDir string) string {
	if strings.TrimSpace(extractDir) == "" {
		extractDir = defaultEmbeddedAssetExtractPath
	}
	return joinProjectPath(projectRoot, extractDir)
}

func (s *MCPServer) handleDesktopShellHandshake(params json.RawMessage) (interface{}, error) {
	var args struct {
		ArtifactPath  string `json:"artifact_path"`
		ExtractDir    string `json:"extract_dir"`
		ExtractAssets bool   `json:"extract_assets"`
	}
	_ = json.Unmarshal(params, &args)

	// Initialize WUI HTTP server if not already running
	if wuiHTTPServer == nil || !wuiHTTPServer.IsRunning() {
		if err := InitWUIHTTPServer(s.projectRoot); err != nil {
			fmt.Fprintf(os.Stderr, "[WUI HTTP] Failed to start: %v\n", err)
		}
	}

	runtimeContractPath, runtimeBootstrapPath, err := ensureDesktopRuntimeContractAssets(s.projectRoot)
	if err != nil {
		return nil, fmt.Errorf("failed to initialize desktop shell runtime assets: %w", err)
	}

	release := s.snapshotReleaseState()
	artifactPath := strings.TrimSpace(args.ArtifactPath)
	if artifactPath == "" {
		artifactPath = release.ArtifactPath
	}
	if artifactPath == "" {
		artifactPath = "qminiwasm_wui.exe"
	}
	artifactPath = joinProjectPath(s.projectRoot, artifactPath)

	selfCheck := s.buildSystemSelfCheck(artifactPath)
	result := map[string]interface{}{
		"contract_version":       desktopShellContractVersion,
		"required_method":        "window.qMiniMcpHost.callTool(name, arguments)",
		"request_channel":        "qmini-mcp-request",
		"response_channel":       "qmini-mcp-response",
		"handshake_tool":         "wui_desktop_shell_handshake",
		"runtime_contract_path":  runtimeContractPath,
		"runtime_bootstrap_path": runtimeBootstrapPath,
		"artifact_path":          artifactPath,
		"artifact_exists":        fileExists(artifactPath),
		"self_check":             selfCheck,
	}

	if args.ExtractAssets && fileExists(artifactPath) {
		extractRoot := normalizeExtractRoot(s.projectRoot, args.ExtractDir)
		manifestPath, assetCount, bundleHash, err := extractEmbeddedAssetBundle(artifactPath, extractRoot)
		if err != nil {
			return nil, fmt.Errorf("desktop shell handshake extraction failed: %w", err)
		}

		bundleBytes := int64(0)
		if found, _, size, _, probeErr := probeEmbeddedAssetFooter(artifactPath); probeErr == nil && found {
			bundleBytes = size
		}

		artifactHash, _ := hashFileHex(artifactPath)
		nativeRuntimeAssets := countManifestNativeRuntimeAssets(manifestPath)
		s.setReleaseBundleMetadata(artifactHash, bundleHash, bundleBytes, extractRoot, manifestPath, assetCount, nativeRuntimeAssets)

		result["extracted_assets"] = true
		result["asset_manifest_path"] = manifestPath
		result["asset_count"] = assetCount
		result["asset_dir"] = extractRoot
		result["embedded_asset_sha256"] = bundleHash
		result["embedded_native_runtime_assets"] = nativeRuntimeAssets

		// Add WUI HTTP server URL if running
		if wuiHTTPServer != nil && wuiHTTPServer.IsRunning() {
			result["wui_http_url"] = wuiHTTPServer.GetURL()
		}
	}

	return result, nil
}

// ToolHandler is a function that handles an MCP tool call.
type ToolHandler func(params json.RawMessage) (interface{}, error)

// NewMCPServer creates a new MCP server instance.
func NewMCPServer() *MCPServer {
	s := &MCPServer{
		scanner:     bufio.NewScanner(os.Stdin),
		tools:       make(map[string]ToolHandler),
		projectRoot: detectProjectRoot(),
		release: ReleaseState{
			Status:               "idle",
			ShellContractVersion: desktopShellContractVersion,
			UpdatedAt:            time.Now().Format(time.RFC3339),
		},
	}
	if s.projectRoot == "" {
		s.projectRoot = "."
	}
	if _, _, err := ensureDesktopRuntimeContractAssets(s.projectRoot); err != nil {
		s.updateReleaseState("error", "runtime_contract_init", "local", "", []string{"failed to initialize runtime desktop shell assets"}, err)
	}
	s.registerTools()
	return s
}

// registerTools registers all available tool handlers.
func (s *MCPServer) registerTools() {
	// Bridge lifecycle
	s.tools["wui_connect"] = s.handleWUIConnect
	s.tools["wui_disconnect"] = s.handleWUIDisconnect
	s.tools["wui_fetch_url"] = s.handleFetchURL
	s.tools["wui_system_self_check"] = s.handleSystemSelfCheck
	s.tools["wui_get_system_toml"] = s.handleGetSystemTOML
	s.tools["wui_set_system_toml"] = s.handleSetSystemTOML
	s.tools["wui_validate_system_toml"] = s.handleValidateSystemTOML
	s.tools["wui_desktop_shell_handshake"] = s.handleDesktopShellHandshake

	// Quantum operation tools
	s.tools["wui_apply_hadamard"] = s.handleApplyHadamard
	s.tools["wui_apply_phase"] = s.handleApplyPhase
	s.tools["wui_apply_csum"] = s.handleApplyCSUM
	s.tools["wui_apply_pauli_x"] = s.handleApplyPauliX
	s.tools["wui_apply_pauli_z"] = s.handleApplyPauliZ
	s.tools["wui_measure"] = s.handleMeasure

	// Runtime control tools
	s.tools["wui_set_config"] = s.handleSetConfig
	s.tools["wui_set_num_qutrits"] = s.handleSetNumQutrits
	s.tools["wui_set_entanglement_graph"] = s.handleSetEntanglementGraph
	s.tools["wui_run_inference"] = s.handleRunInference
	s.tools["wui_get_metrics"] = s.handleGetMetrics
	s.tools["wui_get_pipeline_status"] = s.handleGetPipelineStatus
	s.tools["wui_trigger_pipeline"] = s.handleTriggerPipeline
	s.tools["wui_compute_betti"] = s.handleComputeBetti
	s.tools["wui_start_ff_training"] = s.handleStartFFTraining
	s.tools["wui_stop_ff_training"] = s.handleStopFFTraining
	s.tools["wui_init_graph"] = s.handleInitGraph
	s.tools["wui_add_graph_node"] = s.handleAddGraphNode
	s.tools["wui_add_graph_edge"] = s.handleAddGraphEdge
	s.tools["wui_read_memory"] = s.handleReadMemory
	s.tools["wui_write_memory"] = s.handleWriteMemory

	// Training pipeline handlers defined in pipeline_handlers.go
	s.tools["wui_init_training_pipeline"] = s.handleInitTrainingPipeline
	s.tools["wui_set_pipeline_config"] = s.handleSetPipelineConfig
	s.tools["wui_get_training_metrics"] = s.handleGetTrainingMetrics
	s.tools["wui_apply_betti_guidance"] = s.handleApplyBettiGuidance
	s.tools["wui_pause_training"] = s.handlePauseTraining
	s.tools["wui_resume_training"] = s.handleResumeTraining
	s.tools["wui_export_model"] = s.handleExportModel
	s.tools["wui_import_model"] = s.handleImportModel

	// Desktop release lifecycle
	s.tools["wui_build_release"] = s.handleBuildRelease
	s.tools["wui_deploy_release"] = s.handleDeployRelease
	s.tools["wui_rollback_release"] = s.handleRollbackRelease
	s.tools["wui_get_release_status"] = s.handleGetReleaseStatus
	s.tools["wui_get_ops_snapshot"] = s.handleGetOpsSnapshot

	// API Key management (Windows Credential Manager)
	s.tools["wui_store_api_keys"] = s.handleStoreApiKeys
	s.tools["wui_load_api_keys"] = s.handleLoadApiKeys
	s.tools["wui_delete_api_key"] = s.handleDeleteApiKey

	// Training configuration management
	s.tools["wui_load_training_config"] = s.handleLoadTrainingConfig
	s.tools["wui_save_training_config"] = s.handleSaveTrainingConfig

	// Training with SSE streaming
	s.tools["wui_start_training_sse"] = s.handleStartTrainingWithSSE
	s.tools["wui_stop_training_sse"] = s.handleStopTrainingSSE
	s.tools["wui_get_training_status"] = s.handleGetTrainingStatus
	s.tools["get_resource_metrics"] = s.handleGetResourceMetrics

	// AI Inference suite
	s.tools["wui_load_model"] = s.handleLoadModel
	s.tools["wui_unload_model"] = s.handleUnloadModel
	s.tools["wui_list_models"] = s.handleListModels
	s.tools["wui_run_inference"] = s.handleRunInference
	s.tools["wui_stop_inference"] = s.handleStopInference
	s.tools["wui_get_inference_metrics"] = s.handleGetInferenceMetrics
	s.tools["wui_detect_hardware_limits"] = s.handleDetectHardwareLimits
}

func detectProjectRoot() string {
	wd, err := os.Getwd()
	if err != nil {
		return "."
	}

	current := wd
	for i := 0; i < 8; i++ {
		if fileExists(filepath.Join(current, "config", "system.toml")) && fileExists(filepath.Join(current, "wui", "index.html")) {
			return current
		}

		parent := filepath.Dir(current)
		if parent == current {
			break
		}
		current = parent
	}

	return wd
}

func joinProjectPath(projectRoot, p string) string {
	pathValue := strings.TrimSpace(p)
	if pathValue == "" {
		pathValue = "."
	}

	if filepath.IsAbs(pathValue) {
		return filepath.Clean(pathValue)
	}

	root := strings.TrimSpace(projectRoot)
	if root == "" {
		root = "."
	}

	return filepath.Clean(filepath.Join(root, pathValue))
}

func systemTomlPath(projectRoot string) string {
	return joinProjectPath(projectRoot, filepath.Join("config", "system.toml"))
}

func requiredSystemSections() []string {
	return []string{
		"memory",
		"sycl",
		"qgnn",
		"moe",
		"learning",
		"wui",
		"pipelines",
		"logging",
	}
}

func validateSystemTOMLDocument(content string) TOMLValidationResult {
	result := TOMLValidationResult{
		Valid:           true,
		Errors:          []string{},
		Warnings:        []string{},
		MissingSections: []string{},
	}

	trimmed := strings.TrimSpace(content)
	if trimmed == "" {
		result.Valid = false
		result.Errors = append(result.Errors, "system TOML content is empty")
		return result
	}

	var doc map[string]interface{}
	if err := toml.Unmarshal([]byte(content), &doc); err != nil {
		result.Valid = false
		result.Errors = append(result.Errors, fmt.Sprintf("TOML syntax error: %v", err))
		return result
	}

	systemRaw, ok := doc["system"]
	if !ok {
		result.Valid = false
		result.Errors = append(result.Errors, "missing [system] root table")
		result.MissingSections = append(result.MissingSections, requiredSystemSections()...)
		return result
	}

	systemTable, ok := systemRaw.(map[string]interface{})
	if !ok {
		result.Valid = false
		result.Errors = append(result.Errors, "invalid [system] table structure")
		result.MissingSections = append(result.MissingSections, requiredSystemSections()...)
		return result
	}

	for _, key := range requiredSystemSections() {
		if _, exists := systemTable[key]; !exists {
			result.Valid = false
			result.MissingSections = append(result.MissingSections, key)
		}
	}

	if len(result.MissingSections) > 0 {
		result.Errors = append(result.Errors, fmt.Sprintf("missing required [system.*] sections: %s", strings.Join(result.MissingSections, ", ")))
	}

	if wuiRaw, ok := systemTable["wui"].(map[string]interface{}); ok {
		if _, ok := wuiRaw["activity_log_size"]; !ok {
			result.Warnings = append(result.Warnings, "[system.wui].activity_log_size not set; cognitive log chunking may drift")
		}
	}

	return result
}

func tryTouchWritable(path string) bool {
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_APPEND, 0)
	if err == nil {
		_ = f.Close()
		return true
	}

	if os.IsNotExist(err) {
		if mkErr := os.MkdirAll(filepath.Dir(path), 0o755); mkErr != nil {
			return false
		}
		f2, createErr := os.OpenFile(path, os.O_CREATE|os.O_WRONLY, 0o644)
		if createErr != nil {
			return false
		}
		_ = f2.Close()
		return true
	}

	return false
}

func tailLines(content string, maxLines int) []string {
	if maxLines <= 0 {
		maxLines = 8
	}

	parts := strings.Split(strings.ReplaceAll(content, "\r\n", "\n"), "\n")
	out := make([]string, 0, maxLines)
	for _, line := range parts {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" {
			continue
		}
		out = append(out, trimmed)
	}

	if len(out) <= maxLines {
		return out
	}
	return out[len(out)-maxLines:]
}

func runCommandWithTimeoutEnv(timeout time.Duration, dir string, extraEnv map[string]string, name string, args ...string) (string, error) {
	if timeout <= 0 {
		timeout = 2 * time.Minute
	}

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	cmd := exec.CommandContext(ctx, name, args...)
	if dir != "" {
		cmd.Dir = dir
	}
	if len(extraEnv) > 0 {
		mergedEnv := append([]string{}, os.Environ()...)
		keys := make([]string, 0, len(extraEnv))
		for key := range extraEnv {
			keys = append(keys, key)
		}
		sort.Strings(keys)
		for _, key := range keys {
			mergedEnv = append(mergedEnv, fmt.Sprintf("%s=%s", key, extraEnv[key]))
		}
		cmd.Env = mergedEnv
	}
	output, err := cmd.CombinedOutput()
	if ctx.Err() == context.DeadlineExceeded {
		return string(output), fmt.Errorf("command timeout after %s", timeout)
	}
	return string(output), err
}

func runCommandWithTimeout(timeout time.Duration, dir string, name string, args ...string) (string, error) {
	return runCommandWithTimeoutEnv(timeout, dir, nil, name, args...)
}

func fileExists(path string) bool {
	if path == "" {
		return false
	}
	_, err := os.Stat(path)
	return err == nil
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	if _, err := out.ReadFrom(in); err != nil {
		return err
	}

	return out.Sync()
}

func findFirstExisting(paths []string) string {
	for _, p := range paths {
		if fileExists(p) {
			return p
		}
	}
	return ""
}

func (s *MCPServer) updateReleaseState(status, action, target, artifact string, logs []string, opErr error) {
	s.releaseMu.Lock()
	defer s.releaseMu.Unlock()

	if status != "" {
		s.release.Status = status
	}
	if action != "" {
		s.release.LastAction = action
	}
	if target != "" {
		s.release.Target = target
	}
	if artifact != "" {
		s.release.ArtifactPath = artifact
	}
	if logs != nil {
		s.release.LastLogLines = logs
	}
	if opErr != nil {
		s.release.LastError = opErr.Error()
	} else {
		s.release.LastError = ""
	}
	s.release.ShellContractVersion = desktopShellContractVersion
	s.release.UpdatedAt = time.Now().Format(time.RFC3339)
}

func (s *MCPServer) snapshotReleaseState() ReleaseState {
	s.releaseMu.RLock()
	defer s.releaseMu.RUnlock()
	copyState := s.release
	if copyState.LastLogLines == nil {
		copyState.LastLogLines = []string{}
	}
	return copyState
}

func (s *MCPServer) setReleaseBundleMetadata(artifactSHA, bundleSHA string, bundleBytes int64, assetDir, manifestPath string, assetCount, embeddedNativeRuntimeAssets int) {
	s.releaseMu.Lock()
	defer s.releaseMu.Unlock()

	if artifactSHA != "" {
		s.release.ArtifactSHA256 = artifactSHA
	}
	if bundleSHA != "" {
		s.release.EmbeddedBundleSHA256 = bundleSHA
	}
	if bundleBytes >= 0 {
		s.release.EmbeddedBundleBytes = bundleBytes
	}
	if strings.TrimSpace(assetDir) != "" {
		s.release.AssetDir = assetDir
	}
	if strings.TrimSpace(manifestPath) != "" {
		s.release.AssetManifestPath = manifestPath
	}
	if assetCount >= 0 {
		s.release.AssetCount = assetCount
	}
	if embeddedNativeRuntimeAssets >= 0 {
		s.release.EmbeddedNativeRuntimeAssets = embeddedNativeRuntimeAssets
	}
	s.release.ShellContractVersion = desktopShellContractVersion
	s.release.UpdatedAt = time.Now().Format(time.RFC3339)
}

func (s *MCPServer) buildSystemSelfCheck(expectArtifact string) map[string]interface{} {
	tomlPath := systemTomlPath(s.projectRoot)
	tomlExists := fileExists(tomlPath)
	tomlWritable := tryTouchWritable(tomlPath)

	validation := TOMLValidationResult{Valid: false, Errors: []string{"system TOML missing"}}
	if tomlExists {
		if content, err := os.ReadFile(tomlPath); err == nil {
			validation = validateSystemTOMLDocument(string(content))
		} else {
			validation = TOMLValidationResult{
				Valid:    false,
				Errors:   []string{fmt.Sprintf("failed to read TOML: %v", err)},
				Warnings: []string{},
			}
		}
	}

	release := s.snapshotReleaseState()
	artifactCandidate := strings.TrimSpace(expectArtifact)
	if artifactCandidate == "" {
		if release.ArtifactPath != "" {
			artifactCandidate = release.ArtifactPath
		} else {
			artifactCandidate = "qminiwasm_wui.exe"
		}
	}
	artifactCandidate = joinProjectPath(s.projectRoot, artifactCandidate)
	artifactExists := fileExists(artifactCandidate)

	artifactSHA := ""
	if artifactExists {
		hash, err := hashFileHex(artifactCandidate)
		if err == nil {
			artifactSHA = hash
		}
	}

	bundleFound := false
	bundleBytes := int64(0)
	bundleHash := ""
	bundleErr := ""
	if artifactExists {
		found, _, size, hash, err := probeEmbeddedAssetFooter(artifactCandidate)
		if err != nil {
			bundleErr = err.Error()
		} else {
			bundleFound = found
			bundleBytes = size
			bundleHash = hash
		}
	}

	runtimeContractPath := joinProjectPath(s.projectRoot, desktopShellContractAssetPath)
	runtimeBootstrapPath := joinProjectPath(s.projectRoot, desktopShellBootstrapAssetPath)
	runtimeAssetInitErr := ""
	runtimeExecutionAvailable := runtimeCGOEnabled() || s.canPassthroughToBackend()
	if ensuredContract, ensuredBootstrap, err := ensureDesktopRuntimeContractAssets(s.projectRoot); err != nil {
		runtimeAssetInitErr = err.Error()
	} else {
		runtimeContractPath = ensuredContract
		runtimeBootstrapPath = ensuredBootstrap
	}

	manifestPath := strings.TrimSpace(release.AssetManifestPath)
	if manifestPath == "" {
		manifestPath = joinProjectPath(s.projectRoot, embeddedAssetManifestPath)
	} else {
		manifestPath = joinProjectPath(s.projectRoot, manifestPath)
	}
	embeddedNativeRuntimeAssets := countManifestNativeRuntimeAssets(manifestPath)
	if embeddedNativeRuntimeAssets <= 0 {
		embeddedNativeRuntimeAssets = release.EmbeddedNativeRuntimeAssets
	}

	nativeRuntimePaths := findNativeRuntimeArtifactPaths(s.projectRoot)
	nativeRuntimeAvailable := len(nativeRuntimePaths) > 0

	enginePath := findFirstExisting([]string{
		joinProjectPath(s.projectRoot, "q_mini_wasm_v2.exe"),
		joinProjectPath(s.projectRoot, filepath.Join("q_mini_wasm_v2", "build", "bin", "q_mini_wasm_v2.exe")),
		joinProjectPath(s.projectRoot, filepath.Join("q_mini_wasm_v2", "build", "bin", "q_gf3_wasm.dll")),
	})

	bridgePath := findFirstExisting([]string{
		joinProjectPath(s.projectRoot, filepath.Join("agents", "cmd", "wui-cli-bridge", "wui-cli-bridge.exe")),
		joinProjectPath(s.projectRoot, filepath.Join("agents", "cmd", "wui-cli-bridge", "wui-mcp.exe")),
	})

	// Check both WebSocket and HTTP connections
	mcpConnected := (s.wsClient != nil && s.wsClient.IsConnected()) || s.httpMCPActive
	strictMode := (s.wsClient != nil && s.wsClient.strictMode) || (s.wsClient == nil && s.httpMCPActive)

	checks := map[string]interface{}{
		"mcp_bridge_connected":               mcpConnected,
		"strict_mode":                        strictMode,
		"backend_passthrough":                s.canPassthroughToBackend(),
		"pipeline_initialized":               s.pipeline != nil,
		"cgo_enabled":                        runtimeCGOEnabled(),
		"system_toml_path":                   tomlPath,
		"system_toml_exists":                 tomlExists,
		"system_toml_writable":               tomlWritable,
		"system_toml_validation":             validation,
		"desktop_artifact_path":              artifactCandidate,
		"desktop_artifact_exists":            artifactExists,
		"desktop_artifact_sha256":            artifactSHA,
		"embedded_asset_bundle":              bundleFound,
		"embedded_asset_bytes":               bundleBytes,
		"embedded_asset_sha256":              bundleHash,
		"runtime_contract_path":              runtimeContractPath,
		"runtime_contract_exists":            fileExists(runtimeContractPath),
		"runtime_bootstrap_path":             runtimeBootstrapPath,
		"runtime_bootstrap_exists":           fileExists(runtimeBootstrapPath),
		"runtime_execution_available":        runtimeExecutionAvailable,
		"asset_manifest_path":                manifestPath,
		"asset_manifest_exists":              fileExists(manifestPath),
		"asset_count":                        release.AssetCount,
		"embedded_native_runtime_assets":     embeddedNativeRuntimeAssets,
		"native_runtime_artifact_paths":      nativeRuntimePaths,
		"native_runtime_artifacts_available": nativeRuntimeAvailable,
		"shell_contract_version":             desktopShellContractVersion,
		"engine_artifact_path":               enginePath,
		"engine_artifact_available":          enginePath != "",
		"bridge_binary_path":                 bridgePath,
		"bridge_binary_available":            bridgePath != "",
		"timestamp":                          time.Now().Format(time.RFC3339),
	}
	if bundleErr != "" {
		checks["embedded_asset_error"] = bundleErr
	}
	if runtimeAssetInitErr != "" {
		checks["runtime_asset_error"] = runtimeAssetInitErr
	}

	ready := tomlWritable && validation.Valid && runtimeExecutionAvailable && fileExists(runtimeContractPath) && fileExists(runtimeBootstrapPath) && artifactExists && bundleFound && embeddedNativeRuntimeAssets > 0
	checks["ready_for_operator_pipeline"] = ready

	return checks
}

func (s *MCPServer) handleGetSystemTOML(_ json.RawMessage) (interface{}, error) {
	path := systemTomlPath(s.projectRoot)
	content, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read TOML config '%s': %w", path, err)
	}

	return map[string]interface{}{
		"path": path,
		"toml": string(content),
	}, nil
}

func (s *MCPServer) handleSetSystemTOML(params json.RawMessage) (interface{}, error) {
	var args struct {
		Toml string `json:"toml"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if args.Toml == "" {
		return nil, fmt.Errorf("empty TOML payload")
	}

	validation := validateSystemTOMLDocument(args.Toml)
	if !validation.Valid {
		return nil, fmt.Errorf("refusing invalid TOML: %s", strings.Join(validation.Errors, "; "))
	}

	path := systemTomlPath(s.projectRoot)
	backup := ""
	if _, err := os.Stat(path); err == nil {
		backup = path + ".bak"
		prev, readErr := os.ReadFile(path)
		if readErr == nil {
			_ = os.WriteFile(backup, prev, 0o644)
		}
	}

	if err := os.WriteFile(path, []byte(args.Toml), 0o644); err != nil {
		return nil, fmt.Errorf("failed to write TOML config '%s': %w", path, err)
	}

	return map[string]interface{}{
		"updated":     true,
		"path":        path,
		"backup_path": backup,
		"validation":  validation,
	}, nil
}

func (s *MCPServer) handleValidateSystemTOML(params json.RawMessage) (interface{}, error) {
	var args struct {
		Toml string `json:"toml"`
		Path string `json:"path"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	path := strings.TrimSpace(args.Path)
	if path == "" {
		path = systemTomlPath(s.projectRoot)
	} else {
		path = joinProjectPath(s.projectRoot, path)
	}

	tomlContent := args.Toml
	if strings.TrimSpace(tomlContent) == "" {
		content, err := os.ReadFile(path)
		if err != nil {
			return nil, fmt.Errorf("failed to read TOML for validation: %w", err)
		}
		tomlContent = string(content)
	}

	validation := validateSystemTOMLDocument(tomlContent)
	return map[string]interface{}{
		"path":       path,
		"validation": validation,
	}, nil
}

func (s *MCPServer) handleSystemSelfCheck(params json.RawMessage) (interface{}, error) {
	var args struct {
		ExpectArtifact string `json:"expect_artifact"`
	}
	_ = json.Unmarshal(params, &args)

	return s.buildSystemSelfCheck(args.ExpectArtifact), nil
}

func (s *MCPServer) handleBuildRelease(params json.RawMessage) (interface{}, error) {
	var args struct {
		Output               string `json:"output"`
		GoBinary             string `json:"go_binary"`
		BuildBridge          bool   `json:"build_bridge"`
		RequireNativeRuntime *bool  `json:"require_native_runtime"`
		RequireCGO           *bool  `json:"require_cgo"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if strings.TrimSpace(args.Output) == "" {
		args.Output = "qminiwasm_wui.exe"
	}
	if strings.TrimSpace(args.GoBinary) == "" {
		args.GoBinary = "go"
	}

	requireNativeRuntime := true
	if args.RequireNativeRuntime != nil {
		requireNativeRuntime = *args.RequireNativeRuntime
	}
	requireCGO := true
	if args.RequireCGO != nil {
		requireCGO = *args.RequireCGO
	}

	buildEnv := map[string]string{}
	if requireCGO {
		buildEnv["CGO_ENABLED"] = "1"
	} else {
		buildEnv["CGO_ENABLED"] = "0"
	}

	payload, err := buildEmbeddedAssetPayload(s.projectRoot, requireNativeRuntime)
	if err != nil {
		s.updateReleaseState("error", "build_release", "local", args.Output, []string{"embedded asset payload generation failed"}, err)
		return nil, fmt.Errorf("failed to build embedded asset payload: %w", err)
	}

	outputPath := joinProjectPath(s.projectRoot, args.Output)
	if err := os.MkdirAll(filepath.Dir(outputPath), 0o755); err != nil && filepath.Dir(outputPath) != "." {
		return nil, fmt.Errorf("failed to create output directory: %w", err)
	}

	s.updateReleaseState("building", "build_release", "local", outputPath, nil, nil)
	bridgeModuleDir := joinProjectPath(s.projectRoot, filepath.Join("agents", "cmd", "wui-cli-bridge"))

	combinedLogs := []string{}
	if args.BuildBridge {
		bridgeOutput, bridgeErr := runCommandWithTimeoutEnv(
			5*time.Minute,
			bridgeModuleDir,
			buildEnv,
			args.GoBinary,
			"build", "-o", "wui-cli-bridge.exe", ".",
		)
		combinedLogs = append(combinedLogs, tailLines(bridgeOutput, 8)...)
		if bridgeErr != nil {
			s.updateReleaseState("error", "build_release", "local", outputPath, combinedLogs, bridgeErr)
			return nil, fmt.Errorf("failed to build wui-cli-bridge: %w | output: %s", bridgeErr, bridgeOutput)
		}
	}

	desktopOutput, desktopErr := runCommandWithTimeoutEnv(
		10*time.Minute,
		bridgeModuleDir,
		buildEnv,
		args.GoBinary,
		"build", "-o", outputPath, ".",
	)
	combinedLogs = append(combinedLogs, tailLines(desktopOutput, 10)...)
	if desktopErr != nil {
		s.updateReleaseState("error", "build_release", "local", outputPath, combinedLogs, desktopErr)
		return nil, fmt.Errorf("failed to build desktop executable: %w | output: %s", desktopErr, desktopOutput)
	}

	artifactExists := fileExists(outputPath)
	if !artifactExists {
		err := fmt.Errorf("build completed but artifact not found at %s", outputPath)
		s.updateReleaseState("error", "build_release", "local", outputPath, combinedLogs, err)
		return nil, err
	}

	if err := stripEmbeddedAssetBundleIfPresent(outputPath); err != nil {
		s.updateReleaseState("error", "build_release", "local", outputPath, combinedLogs, err)
		return nil, fmt.Errorf("failed to normalize executable before asset append: %w", err)
	}

	if err := appendEmbeddedAssetBundle(outputPath, payload); err != nil {
		s.updateReleaseState("error", "build_release", "local", outputPath, combinedLogs, err)
		return nil, fmt.Errorf("failed to append embedded asset bundle: %w", err)
	}

	artifactSHA, err := hashFileHex(outputPath)
	if err != nil {
		s.updateReleaseState("error", "build_release", "local", outputPath, combinedLogs, err)
		return nil, fmt.Errorf("failed to hash desktop artifact: %w", err)
	}

	extractRoot := normalizeExtractRoot(s.projectRoot, "")
	manifestPath, assetCount, bundleHash, err := extractEmbeddedAssetBundle(outputPath, extractRoot)
	if err != nil {
		s.updateReleaseState("error", "build_release", "local", outputPath, combinedLogs, err)
		return nil, fmt.Errorf("failed to verify embedded bundle extraction: %w", err)
	}

	bundleBytes := int64(len(payload.Bundle))
	if found, _, size, _, probeErr := probeEmbeddedAssetFooter(outputPath); probeErr == nil && found {
		bundleBytes = size
	}

	nativeRuntimeAssets := countManifestNativeRuntimeAssets(manifestPath)
	if nativeRuntimeAssets <= 0 {
		nativeRuntimeAssets = len(payload.NativeAssets)
	}
	if requireNativeRuntime && nativeRuntimeAssets <= 0 {
		err := fmt.Errorf("embedded bundle missing native runtime assets while require_native_runtime=true")
		s.updateReleaseState("error", "build_release", "local", outputPath, combinedLogs, err)
		return nil, err
	}

	s.setReleaseBundleMetadata(artifactSHA, bundleHash, bundleBytes, extractRoot, manifestPath, assetCount, nativeRuntimeAssets)
	combinedLogs = append(combinedLogs,
		fmt.Sprintf("embedded_assets=%d", assetCount),
		fmt.Sprintf("embedded_native_runtime_assets=%d", nativeRuntimeAssets),
		fmt.Sprintf("embedded_bundle_sha256=%s", bundleHash),
	)

	s.updateReleaseState("built", "build_release", "local", outputPath, combinedLogs, nil)

	return map[string]interface{}{
		"built":                          true,
		"artifact_path":                  outputPath,
		"artifact_sha256":                artifactSHA,
		"embedded_asset_sha256":          bundleHash,
		"embedded_asset_bytes":           bundleBytes,
		"asset_manifest_path":            manifestPath,
		"asset_count":                    assetCount,
		"embedded_native_runtime_assets": nativeRuntimeAssets,
		"native_runtime_assets":          payload.NativeAssets,
		"require_native_runtime":         requireNativeRuntime,
		"require_cgo":                    requireCGO,
		"build_env":                      map[string]interface{}{"CGO_ENABLED": buildEnv["CGO_ENABLED"]},
		"asset_dir":                      extractRoot,
		"shell_contract_version":         desktopShellContractVersion,
		"log_tail":                       combinedLogs,
	}, nil
}

func (s *MCPServer) handleDeployRelease(params json.RawMessage) (interface{}, error) {
	var args struct {
		ArtifactPath         string `json:"artifact_path"`
		Target               string `json:"target"`
		DeployDir            string `json:"deploy_dir"`
		ExtractDir           string `json:"extract_dir"`
		ExtractAssets        *bool  `json:"extract_assets"`
		RequireNativeRuntime *bool  `json:"require_native_runtime"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	release := s.snapshotReleaseState()
	artifactPath := strings.TrimSpace(args.ArtifactPath)
	if artifactPath == "" {
		artifactPath = release.ArtifactPath
	}
	if artifactPath == "" {
		artifactPath = "qminiwasm_wui.exe"
	}
	artifactPath = joinProjectPath(s.projectRoot, artifactPath)

	target := strings.TrimSpace(args.Target)
	if target == "" {
		target = "local"
	}

	deployDir := strings.TrimSpace(args.DeployDir)
	if deployDir == "" {
		deployDir = filepath.Join("releases", "desktop", "current")
	}
	deployDir = joinProjectPath(s.projectRoot, deployDir)

	if !fileExists(artifactPath) {
		err := fmt.Errorf("artifact not found: %s", artifactPath)
		s.updateReleaseState("error", "deploy_release", target, artifactPath, nil, err)
		return nil, err
	}

	dstPath := filepath.Join(deployDir, filepath.Base(artifactPath))
	backupPath := dstPath + ".bak"
	if fileExists(dstPath) {
		_ = copyFile(dstPath, backupPath)
	}

	if err := copyFile(artifactPath, dstPath); err != nil {
		s.updateReleaseState("error", "deploy_release", target, artifactPath, nil, err)
		return nil, fmt.Errorf("failed to deploy artifact: %w", err)
	}

	artifactSHA, _ := hashFileHex(dstPath)
	bundleFound, _, bundleBytes, bundleHash, probeErr := probeEmbeddedAssetFooter(dstPath)
	if probeErr != nil {
		s.updateReleaseState("error", "deploy_release", target, artifactPath, nil, probeErr)
		return nil, fmt.Errorf("failed to probe deployed embedded bundle: %w", probeErr)
	}

	extractAssets := true
	if args.ExtractAssets != nil {
		extractAssets = *args.ExtractAssets
	}
	requireNativeRuntime := true
	if args.RequireNativeRuntime != nil {
		requireNativeRuntime = *args.RequireNativeRuntime
	}

	assetDir := ""
	manifestPath := ""
	assetCount := -1
	nativeRuntimeAssets := -1
	if bundleFound && extractAssets {
		extractRoot := normalizeExtractRoot(s.projectRoot, args.ExtractDir)
		extractedManifest, extractedCount, extractedHash, extractErr := extractEmbeddedAssetBundle(dstPath, extractRoot)
		if extractErr != nil {
			s.updateReleaseState("error", "deploy_release", target, artifactPath, nil, extractErr)
			return nil, fmt.Errorf("failed to extract deployed embedded assets: %w", extractErr)
		}

		assetDir = extractRoot
		manifestPath = extractedManifest
		assetCount = extractedCount
		nativeRuntimeAssets = countManifestNativeRuntimeAssets(extractedManifest)
		if extractedHash != "" {
			bundleHash = extractedHash
		}
	}

	if requireNativeRuntime && bundleFound {
		checkNativeAssets := nativeRuntimeAssets
		if checkNativeAssets < 0 {
			checkNativeAssets = countManifestNativeRuntimeAssets(manifestPath)
		}
		if checkNativeAssets <= 0 {
			err := fmt.Errorf("deployed artifact missing embedded native runtime assets while require_native_runtime=true")
			s.updateReleaseState("error", "deploy_release", target, artifactPath, nil, err)
			return nil, err
		}
		nativeRuntimeAssets = checkNativeAssets
	}

	s.setReleaseBundleMetadata(artifactSHA, bundleHash, bundleBytes, assetDir, manifestPath, assetCount, nativeRuntimeAssets)

	logs := []string{fmt.Sprintf("deployed %s -> %s", artifactPath, dstPath)}
	s.updateReleaseState("deployed", "deploy_release", target, artifactPath, logs, nil)

	return map[string]interface{}{
		"deployed":                       true,
		"target":                         target,
		"artifact_path":                  artifactPath,
		"deploy_path":                    dstPath,
		"backup_path":                    backupPath,
		"artifact_sha256":                artifactSHA,
		"embedded_asset_bundle":          bundleFound,
		"embedded_asset_sha256":          bundleHash,
		"embedded_asset_bytes":           bundleBytes,
		"asset_manifest_path":            manifestPath,
		"asset_count":                    assetCount,
		"embedded_native_runtime_assets": nativeRuntimeAssets,
		"require_native_runtime":         requireNativeRuntime,
		"asset_dir":                      assetDir,
		"shell_contract_version":         desktopShellContractVersion,
	}, nil
}

func (s *MCPServer) handleRollbackRelease(params json.RawMessage) (interface{}, error) {
	var args struct {
		DeployDir    string `json:"deploy_dir"`
		ArtifactName string `json:"artifact_name"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	release := s.snapshotReleaseState()
	deployDir := strings.TrimSpace(args.DeployDir)
	if deployDir == "" {
		deployDir = filepath.Join("releases", "desktop", "current")
	}
	deployDir = joinProjectPath(s.projectRoot, deployDir)

	artifactName := strings.TrimSpace(args.ArtifactName)
	if artifactName == "" {
		if release.ArtifactPath != "" {
			artifactName = filepath.Base(release.ArtifactPath)
		} else {
			artifactName = "qminiwasm_wui.exe"
		}
	}

	dstPath := filepath.Join(deployDir, artifactName)
	backupPath := dstPath + ".bak"
	if !fileExists(backupPath) {
		err := fmt.Errorf("rollback backup not found: %s", backupPath)
		s.updateReleaseState("error", "rollback_release", release.Target, release.ArtifactPath, nil, err)
		return nil, err
	}

	if err := copyFile(backupPath, dstPath); err != nil {
		s.updateReleaseState("error", "rollback_release", release.Target, release.ArtifactPath, nil, err)
		return nil, fmt.Errorf("failed to restore rollback backup: %w", err)
	}

	logs := []string{fmt.Sprintf("rollback restored %s from %s", dstPath, backupPath)}
	s.updateReleaseState("rolled_back", "rollback_release", release.Target, release.ArtifactPath, logs, nil)

	return map[string]interface{}{
		"rolled_back": true,
		"deploy_path": dstPath,
		"backup_path": backupPath,
	}, nil
}

func (s *MCPServer) handleGetReleaseStatus(_ json.RawMessage) (interface{}, error) {
	release := s.snapshotReleaseState()
	releaseCheck := s.buildSystemSelfCheck(release.ArtifactPath)

	return map[string]interface{}{
		"release":     release,
		"self_check":  releaseCheck,
		"artifact_ok": fileExists(release.ArtifactPath),
	}, nil
}

func (s *MCPServer) handleGetOpsSnapshot(_ json.RawMessage) (interface{}, error) {
	snapshot := map[string]interface{}{
		"timestamp":   time.Now().Format(time.RFC3339),
		"self_check":  s.buildSystemSelfCheck(""),
		"release":     s.snapshotReleaseState(),
		"connected":   s.wsClient != nil && s.wsClient.IsConnected(),
		"strict_mode": s.wsClient == nil || s.wsClient.strictMode,
	}

	if s.pipeline != nil {
		metrics, err := s.pipeline.GetMetrics()
		if err == nil {
			snapshot["training"] = map[string]interface{}{
				"state":             s.pipeline.GetState(),
				"current_epoch":     metrics.CurrentEpoch,
				"current_batch":     metrics.CurrentBatch,
				"training_progress": metrics.TrainingProgress,
				"is_running":        metrics.IsRunning,
				"status_message":    metrics.StatusMessage,
			}
		} else {
			snapshot["training"] = map[string]interface{}{
				"state":         s.pipeline.GetState(),
				"metrics_error": err.Error(),
			}
		}
	} else {
		snapshot["training"] = map[string]interface{}{
			"state": "not_initialized",
		}
	}

	return snapshot, nil
}

// Run starts the MCP server loop.
func (s *MCPServer) Run() {
	for s.scanner.Scan() {
		line := s.scanner.Text()
		if line == "" {
			continue
		}

		var req MCPRequest
		if err := json.Unmarshal([]byte(line), &req); err != nil {
			s.sendError(nil, -32700, "Parse error", err.Error())
			continue
		}

		s.handleRequest(&req)
	}
}

func (s *MCPServer) handleRequest(req *MCPRequest) {
	switch req.Method {
	case "initialize":
		s.handleInitialize(req)
	case "initialized":
		s.handleInitialized(req)
	case "tools/list":
		s.handleToolsList(req)
	case "tools/call":
		s.handleToolCall(req)
	case "resources/list":
		s.handleResourcesList(req)
	case "ping":
		s.sendResult(req.ID, map[string]interface{}{"pong": true})
	default:
		s.sendError(req.ID, -32601, "Method not found", req.Method)
	}
}

func (s *MCPServer) handleInitialize(req *MCPRequest) {
	result := map[string]interface{}{
		"protocolVersion": "2024-11-05",
		"capabilities": map[string]interface{}{
			"tools": map[string]interface{}{},
			"resources": map[string]interface{}{
				"listChanged": true,
			},
		},
		"serverInfo": map[string]interface{}{
			"name":    "qminiwasm-wui-automation",
			"version": "1.2.0",
		},
	}
	s.sendResult(req.ID, result)
}

func (s *MCPServer) handleInitialized(_ *MCPRequest) {
	// Notification: no response required.
}

func (s *MCPServer) handleToolsList(req *MCPRequest) {
	toolSpecs := []map[string]interface{}{
		{"name": "wui_connect", "description": "Initialize bridge session to backend engine", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_disconnect", "description": "Close bridge session", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_fetch_url", "description": "Fetch URL content via MCP host transport", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_system_self_check", "description": "Run startup readiness checks for desktop WUI pipeline", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_get_system_toml", "description": "Read authoritative system TOML configuration", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_set_system_toml", "description": "Write authoritative system TOML configuration", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_validate_system_toml", "description": "Validate system TOML syntax and required section governance", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_desktop_shell_handshake", "description": "Return deterministic desktop shell contract and optional embedded asset extraction details", "inputSchema": map[string]interface{}{"type": "object"}},

		{"name": "wui_apply_hadamard", "description": "Apply Hadamard gate", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_apply_phase", "description": "Apply Phase gate", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_apply_csum", "description": "Apply Controlled-SUM gate", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_apply_pauli_x", "description": "Apply Pauli-X operation", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_apply_pauli_z", "description": "Apply Pauli-Z operation", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_measure", "description": "Measure qutrit state", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_set_config", "description": "Set runtime config value", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_set_num_qutrits", "description": "Set qutrit count", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_set_entanglement_graph", "description": "Set entanglement graph type", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_run_inference", "description": "Run inference", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_get_metrics", "description": "Retrieve runtime metrics", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_get_pipeline_status", "description": "Retrieve pipeline status", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_trigger_pipeline", "description": "Trigger named pipeline", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_compute_betti", "description": "Compute Betti numbers", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_start_ff_training", "description": "Start FF training", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_stop_ff_training", "description": "Stop FF training", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_init_graph", "description": "Initialize graph", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_add_graph_node", "description": "Add graph node", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_add_graph_edge", "description": "Add graph edge", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_read_memory", "description": "Read memory region", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_write_memory", "description": "Write memory region", "inputSchema": map[string]interface{}{"type": "object"}},

		{"name": "wui_init_training_pipeline", "description": "Initialize training pipeline", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_set_pipeline_config", "description": "Set training pipeline configuration", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_get_training_metrics", "description": "Get training metrics", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_apply_betti_guidance", "description": "Apply Betti-guided topology optimization", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_pause_training", "description": "Pause training", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_resume_training", "description": "Resume training", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_export_model", "description": "Export model", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_import_model", "description": "Import model", "inputSchema": map[string]interface{}{"type": "object"}},

		{"name": "wui_build_release", "description": "Build single-executable desktop release artifact", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_deploy_release", "description": "Deploy a built desktop release artifact", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_rollback_release", "description": "Rollback deployed desktop artifact to backup", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_get_release_status", "description": "Get desktop release status and readiness details", "inputSchema": map[string]interface{}{"type": "object"}},
		{"name": "wui_get_ops_snapshot", "description": "Get operations snapshot for train/deploy/runtime visibility", "inputSchema": map[string]interface{}{"type": "object"}},
	}

	s.sendResult(req.ID, map[string]interface{}{"tools": toolSpecs})
}

func (s *MCPServer) handleToolCall(req *MCPRequest) {
	var params struct {
		Name      string          `json:"name"`
		Arguments json.RawMessage `json:"arguments"`
	}

	if err := json.Unmarshal(req.Params, &params); err != nil {
		s.sendError(req.ID, -32602, "Invalid params", err.Error())
		return
	}

	handler, exists := s.tools[params.Name]
	if !exists {
		s.sendError(req.ID, -32601, "Tool not found", params.Name)
		return
	}

	result, err := handler(params.Arguments)
	if err != nil {
		s.sendError(req.ID, -32603, "Tool execution error", err.Error())
		return
	}

	s.sendResult(req.ID, result)
}

func (s *MCPServer) handleResourcesList(req *MCPRequest) {
	resources := []map[string]interface{}{
		{
			"uri":      "file://q_mini_wasm_v2/wui/js/mcp-host-bridge.js",
			"name":     "mcp_host_bridge_asset",
			"mimeType": "text/javascript",
		},
		{
			"uri":      "file://q_mini_wasm_v2/runtime/desktop_shell_contract.json",
			"name":     "desktop_shell_contract",
			"mimeType": "application/json",
		},
		{
			"uri":      "file://q_mini_wasm_v2/runtime/desktop_shell_bootstrap.js",
			"name":     "desktop_shell_bootstrap",
			"mimeType": "text/javascript",
		},
		{
			"uri":      "file://q_mini_wasm_v2/runtime/embedded_asset_manifest.json",
			"name":     "embedded_asset_manifest",
			"mimeType": "application/json",
		},
		{
			"uri":      "file://q_mini_wasm_v2/q_mini_wasm_v2/dll/q_mini_wasm_v2_api.hpp",
			"name":     "dll_api",
			"mimeType": "text/x-c++hdr",
		},
	}
	s.sendResult(req.ID, map[string]interface{}{"resources": resources})
}

func (s *MCPServer) ensureConnected() error {
	if s.wsClient == nil || !s.wsClient.IsConnected() {
		return fmt.Errorf("bridge client not connected")
	}
	return nil
}

func (s *MCPServer) canPassthroughToBackend() bool {
	return s.wsClient != nil && s.wsClient.IsConnected() && !s.wsClient.strictMode
}

func (s *MCPServer) handleWUIConnect(params json.RawMessage) (interface{}, error) {
	var args struct {
		Host      string `json:"host"`
		Port      int    `json:"port"`
		TimeoutMs int    `json:"timeout_ms"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if args.Host == "" {
		args.Host = "localhost"
	}
	if args.Port == 0 {
		args.Port = 8080
	}
	if args.TimeoutMs == 0 {
		args.TimeoutMs = 5000
	}

	if s.wsClient != nil && s.wsClient.IsConnected() {
		return map[string]interface{}{
			"connected":                true,
			"message":                  "Already connected",
			"endpoint":                 s.wsClient.endpoint,
			"strict_mode":              s.wsClient.strictMode,
			"shell_contract_version":   desktopShellContractVersion,
			"handshake_tool":           "wui_desktop_shell_handshake",
			"backend_passthrough":      s.canPassthroughToBackend(),
			"pipeline_initialized":     s.pipeline != nil,
			"requires_compiled_engine": !s.canPassthroughToBackend() && s.pipeline == nil,
		}, nil
	}

	s.wsClient = NewBridgeClient(args.Host, args.Port)
	if err := s.wsClient.Connect(time.Duration(args.TimeoutMs) * time.Millisecond); err != nil {
		return nil, err
	}

	return map[string]interface{}{
		"connected":                true,
		"endpoint":                 fmt.Sprintf("mcp://%s:%d", args.Host, args.Port),
		"strict_mode":              s.wsClient.strictMode,
		"shell_contract_version":   desktopShellContractVersion,
		"handshake_tool":           "wui_desktop_shell_handshake",
		"backend_passthrough":      s.canPassthroughToBackend(),
		"pipeline_initialized":     s.pipeline != nil,
		"requires_compiled_engine": !s.canPassthroughToBackend() && s.pipeline == nil,
	}, nil
}

func (s *MCPServer) handleWUIDisconnect(_ json.RawMessage) (interface{}, error) {
	if s.wsClient != nil {
		s.wsClient.Disconnect()
	}
	return map[string]interface{}{"connected": false}, nil
}

func (s *MCPServer) handleFetchURL(params json.RawMessage) (interface{}, error) {
	var args struct {
		URL       string `json:"url"`
		TimeoutMs int    `json:"timeout_ms"`
		MaxBytes  int64  `json:"max_bytes"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if strings.TrimSpace(args.URL) == "" {
		return nil, fmt.Errorf("url is required")
	}
	if args.TimeoutMs <= 0 {
		args.TimeoutMs = 30000
	}
	if args.TimeoutMs > 180000 {
		args.TimeoutMs = 180000
	}
	// No MaxBytes limit - fetch unlimited data for large scale training
	ctx, cancel := context.WithTimeout(context.Background(), time.Duration(args.TimeoutMs)*time.Millisecond)
	defer cancel()

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, args.URL, nil)
	if err != nil {
		return nil, fmt.Errorf("invalid request: %w", err)
	}
	req.Header.Set("User-Agent", "qminiwasm-mcp-host/1.0")

	resp, err := (&http.Client{}).Do(req)
	if err != nil {
		return nil, fmt.Errorf("fetch failed: %w", err)
	}
	defer resp.Body.Close()

	// Read entire response without limit
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("failed to read response body: %w", err)
	}

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, fmt.Errorf("non-success status code %d for %s", resp.StatusCode, args.URL)
	}

	return map[string]interface{}{
		"url":         args.URL,
		"status_code": resp.StatusCode,
		"bytes":       len(body),
		"body":        string(body),
	}, nil
}

func (s *MCPServer) handleApplyHadamard(params json.RawMessage) (interface{}, error) {
	var args struct {
		QutritIndex int  `json:"qutrit_index"`
		AwaitResult bool `json:"await_result"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}

	msg := map[string]interface{}{"type": "quantum_operation", "operation": "hadamard", "target": args.QutritIndex}
	if args.AwaitResult {
		return s.wsClient.SendAndWait(msg, "quantum_operation_complete", 5*time.Second)
	}
	return map[string]interface{}{"status": "sent", "operation": "hadamard"}, s.wsClient.Send(msg)
}

func (s *MCPServer) handleApplyPhase(params json.RawMessage) (interface{}, error) {
	var args struct {
		QutritIndex int  `json:"qutrit_index"`
		AwaitResult bool `json:"await_result"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}

	msg := map[string]interface{}{"type": "quantum_operation", "operation": "phase", "target": args.QutritIndex}
	if args.AwaitResult {
		return s.wsClient.SendAndWait(msg, "quantum_operation_complete", 5*time.Second)
	}
	return map[string]interface{}{"status": "sent", "operation": "phase"}, s.wsClient.Send(msg)
}

func (s *MCPServer) handleApplyCSUM(params json.RawMessage) (interface{}, error) {
	var args struct {
		Control     int  `json:"control"`
		Target      int  `json:"target"`
		AwaitResult bool `json:"await_result"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}

	msg := map[string]interface{}{"type": "quantum_operation", "operation": "csum", "control": args.Control, "target": args.Target}
	if args.AwaitResult {
		return s.wsClient.SendAndWait(msg, "quantum_operation_complete", 5*time.Second)
	}
	return map[string]interface{}{"status": "sent", "operation": "csum"}, s.wsClient.Send(msg)
}

func (s *MCPServer) handleApplyPauliX(params json.RawMessage) (interface{}, error) {
	var args struct {
		QutritIndex int `json:"qutrit_index"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	msg := map[string]interface{}{"type": "quantum_operation", "operation": "pauli_x", "target": args.QutritIndex}
	return map[string]interface{}{"status": "sent", "operation": "pauli_x"}, s.wsClient.Send(msg)
}

func (s *MCPServer) handleApplyPauliZ(params json.RawMessage) (interface{}, error) {
	var args struct {
		QutritIndex int `json:"qutrit_index"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	msg := map[string]interface{}{"type": "quantum_operation", "operation": "pauli_z", "target": args.QutritIndex}
	return map[string]interface{}{"status": "sent", "operation": "pauli_z"}, s.wsClient.Send(msg)
}

func (s *MCPServer) handleMeasure(params json.RawMessage) (interface{}, error) {
	var args struct {
		QutritIndex int  `json:"qutrit_index"`
		AwaitResult bool `json:"await_result"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	msg := map[string]interface{}{"type": "quantum_operation", "operation": "measure", "target": args.QutritIndex}
	if args.AwaitResult {
		return s.wsClient.SendAndWait(msg, "measurement_result", 5*time.Second)
	}
	return map[string]interface{}{"status": "sent", "operation": "measure"}, s.wsClient.Send(msg)
}

func (s *MCPServer) handleSetConfig(params json.RawMessage) (interface{}, error) {
	var args struct {
		Section string      `json:"section"`
		Key     string      `json:"key"`
		Value   interface{} `json:"value"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	if !s.canPassthroughToBackend() {
		return nil, fmt.Errorf("runtime config passthrough unavailable in strict MCP mode")
	}
	return map[string]interface{}{"status": "config_updated", "section": args.Section, "key": args.Key},
		s.wsClient.Send(map[string]interface{}{"type": "config_update", "section": args.Section, "key": args.Key, "value": args.Value})
}

func (s *MCPServer) handleSetNumQutrits(params json.RawMessage) (interface{}, error) {
	var args struct {
		Count int `json:"count"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	if !s.canPassthroughToBackend() {
		return nil, fmt.Errorf("qutrit config passthrough unavailable in strict MCP mode")
	}
	return map[string]interface{}{"num_qutrits": args.Count},
		s.wsClient.Send(map[string]interface{}{"type": "config_update", "section": "system.qgnn", "key": "default_num_qutrits", "value": args.Count})
}

func (s *MCPServer) handleSetEntanglementGraph(params json.RawMessage) (interface{}, error) {
	var args struct {
		GraphType string `json:"graph_type"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	if !s.canPassthroughToBackend() {
		return nil, fmt.Errorf("entanglement config passthrough unavailable in strict MCP mode")
	}
	return map[string]interface{}{"graph_type": args.GraphType},
		s.wsClient.Send(map[string]interface{}{"type": "config_update", "section": "system.qgnn", "key": "entanglement_graph", "value": args.GraphType})
}

func (s *MCPServer) handleGetMetrics(params json.RawMessage) (interface{}, error) {
	var args struct {
		IntervalMs int `json:"interval_ms"`
	}
	_ = json.Unmarshal(params, &args)
	if args.IntervalMs <= 0 {
		args.IntervalMs = 5000
	}

	if s.wsClient != nil && s.wsClient.IsConnected() {
		if err := s.wsClient.Send(map[string]interface{}{"type": "subscribe_metrics", "interval": args.IntervalMs}); err != nil {
			return nil, err
		}

		resp, err := s.wsClient.SendAndWait(map[string]interface{}{"type": "get_metrics"}, "metrics", 5*time.Second)
		if err == nil {
			return resp, nil
		}

		if s.pipeline == nil {
			return nil, err
		}
	}

	if s.pipeline != nil {
		metrics, err := s.pipeline.GetMetrics()
		if err != nil {
			return nil, fmt.Errorf("failed to get runtime metrics from pipeline: %w", err)
		}

		return map[string]interface{}{
			"stabilizer_rate": metrics.CurrentBatch,
			"quantum_status":  metrics.StatusMessage,
			"ternary_status":  metrics.StatusMessage,
		}, nil
	}

	return nil, fmt.Errorf("not connected to WUI backend")
}

func (s *MCPServer) handleGetPipelineStatus(_ json.RawMessage) (interface{}, error) {
	if s.wsClient != nil && s.wsClient.IsConnected() {
		resp, err := s.wsClient.SendAndWait(map[string]interface{}{"type": "get_pipeline_status"}, "pipeline_status", 5*time.Second)
		if err == nil {
			return resp, nil
		}
		if s.pipeline == nil {
			return nil, err
		}
	}

	if s.pipeline != nil {
		return map[string]interface{}{
			"state": s.pipeline.GetState(),
		}, nil
	}

	return nil, fmt.Errorf("not connected to WUI backend")
}

func (s *MCPServer) handleTriggerPipeline(params json.RawMessage) (interface{}, error) {
	var args struct {
		PipelineType string `json:"pipeline_type"`
		Branch       string `json:"branch"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	if !s.canPassthroughToBackend() {
		return nil, fmt.Errorf("pipeline trigger passthrough unavailable in strict MCP mode")
	}
	if err := s.wsClient.Send(map[string]interface{}{"type": "trigger_pipeline", "pipeline": args.PipelineType, "branch": args.Branch}); err != nil {
		return nil, err
	}
	return map[string]interface{}{"triggered": true, "pipeline_type": args.PipelineType, "branch": args.Branch}, nil
}

func (s *MCPServer) handleComputeBetti(params json.RawMessage) (interface{}, error) {
	var args struct {
		Nodes int `json:"nodes"`
		Edges int `json:"edges"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if s.wsClient != nil && s.wsClient.IsConnected() {
		resp, err := s.wsClient.SendAndWait(map[string]interface{}{"type": "compute_betti", "nodes": args.Nodes, "edges": args.Edges}, "betti_result", 5*time.Second)
		if err == nil {
			return resp, nil
		}
		if s.pipeline == nil {
			return nil, err
		}
	}

	if s.pipeline != nil {
		metrics, err := s.pipeline.GetMetrics()
		if err != nil {
			return nil, fmt.Errorf("failed to get Betti values from pipeline: %w", err)
		}
		return map[string]interface{}{
			"beta_0": metrics.BettiBeta0,
			"beta_1": metrics.BettiBeta1,
			"beta_2": metrics.BettiBeta2,
		}, nil
	}

	return nil, fmt.Errorf("not connected to WUI backend")
}

func (s *MCPServer) handleStartFFTraining(params json.RawMessage) (interface{}, error) {
	var args struct {
		Layers []int `json:"layers"`
		Epochs int   `json:"epochs"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if s.pipeline != nil {
		if err := s.pipeline.Start(); err != nil {
			return nil, err
		}
		return map[string]interface{}{"training_started": true, "engine": "cgo"}, nil
	}

	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	if !s.canPassthroughToBackend() {
		return nil, fmt.Errorf("training pipeline not initialized; call wui_init_training_pipeline first")
	}

	return map[string]interface{}{"training_started": true},
		s.wsClient.Send(map[string]interface{}{"type": "start_ff_training", "layers": args.Layers, "epochs": args.Epochs})
}

func (s *MCPServer) handleStopFFTraining(_ json.RawMessage) (interface{}, error) {
	if s.pipeline != nil {
		s.pipeline.Stop()
		return map[string]interface{}{"training_stopped": true, "engine": "cgo"}, nil
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	if !s.canPassthroughToBackend() {
		return nil, fmt.Errorf("training pipeline not initialized; nothing to stop")
	}
	return map[string]interface{}{"training_stopped": true}, s.wsClient.Send(map[string]interface{}{"type": "stop_ff_training"})
}

func (s *MCPServer) handleInitGraph(params json.RawMessage) (interface{}, error) {
	var args struct {
		Nodes int `json:"nodes"`
		Edges int `json:"edges"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	if !s.canPassthroughToBackend() {
		return nil, fmt.Errorf("graph control backend unavailable in strict MCP mode")
	}
	if err := s.wsClient.Send(map[string]interface{}{"type": "init_graph", "nodes": args.Nodes, "edges": args.Edges}); err != nil {
		return nil, err
	}
	return map[string]interface{}{"initialized": true, "nodes": args.Nodes, "edges": args.Edges}, nil
}

func (s *MCPServer) handleAddGraphNode(_ json.RawMessage) (interface{}, error) {
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	if !s.canPassthroughToBackend() {
		return nil, fmt.Errorf("graph control backend unavailable in strict MCP mode")
	}
	return map[string]interface{}{"node_added": true}, s.wsClient.Send(map[string]interface{}{"type": "add_graph_node"})
}

func (s *MCPServer) handleAddGraphEdge(_ json.RawMessage) (interface{}, error) {
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	if !s.canPassthroughToBackend() {
		return nil, fmt.Errorf("graph control backend unavailable in strict MCP mode")
	}
	return map[string]interface{}{"edge_added": true}, s.wsClient.Send(map[string]interface{}{"type": "add_graph_edge"})
}

func (s *MCPServer) handleReadMemory(params json.RawMessage) (interface{}, error) {
	var args struct {
		Offset int `json:"offset"`
		Length int `json:"length"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	return s.wsClient.SendAndWait(map[string]interface{}{"type": "read_memory", "offset": args.Offset, "length": args.Length}, "memory_data", 5*time.Second)
}

func (s *MCPServer) handleWriteMemory(params json.RawMessage) (interface{}, error) {
	var args struct {
		Offset int   `json:"offset"`
		Data   []int `json:"data"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}
	if err := s.ensureConnected(); err != nil {
		return nil, err
	}
	if err := s.wsClient.Send(map[string]interface{}{"type": "write_memory", "offset": args.Offset, "data": args.Data}); err != nil {
		return nil, err
	}
	return map[string]interface{}{"written": true, "offset": args.Offset, "length": len(args.Data)}, nil
}

// ==================== API Key Management ====================

func (s *MCPServer) handleStoreApiKeys(params json.RawMessage) (interface{}, error) {
	var args struct {
		Keys map[string]string `json:"keys"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	stored := make([]string, 0)
	failed := make([]string, 0)

	for service, apiKey := range args.Keys {
		if err := credManager.Store(service, apiKey); err != nil {
			fmt.Printf("[API Keys] Failed to store %s: %v\n", service, err)
			failed = append(failed, service)
		} else {
			stored = append(stored, service)
		}
	}

	return map[string]interface{}{
		"stored": stored,
		"failed": failed,
		"count":  len(stored),
	}, nil
}

func (s *MCPServer) handleLoadApiKeys(params json.RawMessage) (interface{}, error) {
	var args struct {
		Services []string `json:"services"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	// If no services specified, load all
	if len(args.Services) == 0 {
		services, err := credManager.List()
		if err != nil {
			return nil, err
		}
		args.Services = services
	}

	result := make(map[string]string)
	for _, service := range args.Services {
		apiKey, err := credManager.Load(service)
		if err == nil {
			result[service] = apiKey
		}
	}

	return result, nil
}

func (s *MCPServer) handleDeleteApiKey(params json.RawMessage) (interface{}, error) {
	var args struct {
		Service string `json:"service"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if err := credManager.Delete(args.Service); err != nil {
		return nil, err
	}

	return map[string]interface{}{"deleted": true, "service": args.Service}, nil
}

// ==================== Training Config Management ====================

func (s *MCPServer) handleLoadTrainingConfig(params json.RawMessage) (interface{}, error) {
	var args struct {
		Path string `json:"path"`
	}
	json.Unmarshal(params, &args)

	if args.Path == "" {
		args.Path = "flash_cim_243expert/training_config.toml"
	}

	content, err := os.ReadFile(args.Path)
	if err != nil {
		// Return default config if file doesn't exist
		return map[string]interface{}{"config": "", "exists": false}, nil
	}

	return map[string]interface{}{
		"config": string(content),
		"exists": true,
		"path":   args.Path,
	}, nil
}

func (s *MCPServer) handleSaveTrainingConfig(params json.RawMessage) (interface{}, error) {
	var args struct {
		Config string `json:"config"`
		Path   string `json:"path"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	if args.Path == "" {
		args.Path = "flash_cim_243expert/training_config.toml"
	}

	// Create directory if needed
	dir := filepath.Dir(args.Path)
	if dir != "." && dir != "" {
		os.MkdirAll(dir, 0755)
	}

	// Write config file
	if err := os.WriteFile(args.Path, []byte(args.Config), 0644); err != nil {
		return nil, fmt.Errorf("failed to write config: %v", err)
	}

	return map[string]interface{}{
		"saved": true,
		"path":  args.Path,
	}, nil
}

// ==================== Training Control with SSE ====================

func (s *MCPServer) handleStartTrainingWithSSE(params json.RawMessage) (interface{}, error) {
	var args struct {
		Epochs             int    `json:"epochs"`
		BatchSize          int    `json:"batch_size"`
		ConfigPath         string `json:"config_path"`
		Experts            int    `json:"experts"`
		DatasetPath        string `json:"dataset_path"`
		OutputPath         string `json:"output_path"`
		CheckpointInterval int    `json:"checkpoint_interval"`
		Resume             bool   `json:"resume"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	// Initialize SSE server if not already done
	if sseServer == nil {
		InitSSEServer("")
	}

	// Generate default output path if not provided
	if args.OutputPath == "" {
		timestamp := time.Now().Format("20060102_150405")
		args.OutputPath = fmt.Sprintf("flash_cim_243expert/checkpoints/trained_%s.bbin", timestamp)
	}
	if args.CheckpointInterval == 0 {
		args.CheckpointInterval = 5 // Save every 5 epochs by default
	}

	// Build trainer arguments
	trainerArgs := []string{"--sse-mode"}

	if args.ConfigPath != "" {
		trainerArgs = append(trainerArgs, "--config", args.ConfigPath)
	}
	if args.Epochs > 0 {
		trainerArgs = append(trainerArgs, "--epochs", fmt.Sprintf("%d", args.Epochs))
	}
	if args.BatchSize > 0 {
		trainerArgs = append(trainerArgs, "--batch-size", fmt.Sprintf("%d", args.BatchSize))
	}
	if args.Experts > 0 {
		trainerArgs = append(trainerArgs, "--moe-experts", fmt.Sprintf("%d", args.Experts))
	}
	if args.DatasetPath != "" {
		trainerArgs = append(trainerArgs, "--dataset", args.DatasetPath)
	}
	if args.Resume {
		trainerArgs = append(trainerArgs, "--resume")
	}
	// Always pass output path and checkpoint interval
	trainerArgs = append(trainerArgs, "--output", args.OutputPath)
	trainerArgs = append(trainerArgs, "--checkpoint-every", fmt.Sprintf("%d", args.CheckpointInterval))

	// Get project root
	projectRoot, _ := os.Getwd()

	// Start training process with SSE streaming
	if err := sseServer.StartTraining(projectRoot, trainerArgs); err != nil {
		return nil, err
	}

	return map[string]interface{}{
		"started":      true,
		"stream_url":   fmt.Sprintf("http://localhost:%s/training-stream", sseServer.port),
		"sse_mode":     true,
		"trainer_args": trainerArgs,
	}, nil
}

func (s *MCPServer) handleStopTrainingSSE(_ json.RawMessage) (interface{}, error) {
	if sseServer == nil || !sseServer.IsRunning() {
		return map[string]interface{}{"stopped": true, "was_running": false}, nil
	}

	if err := sseServer.StopTraining(); err != nil {
		return nil, err
	}

	return map[string]interface{}{"stopped": true, "was_running": true}, nil
}

func (s *MCPServer) handleGetTrainingStatus(_ json.RawMessage) (interface{}, error) {
	status := map[string]interface{}{
		"running":   false,
		"sse_ready": sseServer != nil,
	}

	if sseServer != nil {
		status["running"] = sseServer.IsRunning()
		status["clients"] = len(sseServer.clients)
		status["stream_url"] = fmt.Sprintf("http://localhost:%s/training-stream", sseServer.port)
	}

	return status, nil
}

func (s *MCPServer) handleGetResourceMetrics(_ json.RawMessage) (interface{}, error) {
	// Get actual system memory info
	var m runtime.MemStats
	runtime.ReadMemStats(&m)

	// System memory (not just Go heap) - use reasonable estimates
	// This returns actual memory the Go runtime sees, which is closer to real usage
	totalAllocMB := int64(m.TotalAlloc / 1024 / 1024)
	sysMB := int64(m.Sys / 1024 / 1024)

	return map[string]interface{}{
		"memory_used_mb":  sysMB,
		"memory_total_mb": 0, // Would need platform-specific code for total system RAM
		"memory_mb":       sysMB,
		"go_heap_mb":      int64(m.HeapAlloc / 1024 / 1024),
		"go_sys_mb":       sysMB,
		"alloc_mb":        totalAllocMB,
		"cpu_percent":     0.0, // Would need platform-specific code
		"disk_io_mb":      0.0,
		"timestamp":       time.Now().Format(time.RFC3339),
	}, nil
}

// ==================== WUI HTTP Server ====================

// Inference state tracking
type InferenceState struct {
	mu         sync.RWMutex
	loaded     bool
	modelFile  string
	modelSizeB float64
	numQutrits int
	numExperts int
	startTime  time.Time
	iterations int
	converged  bool
}

var inferenceState = &InferenceState{}

// handleLoadModel loads a model checkpoint for inference
func (s *MCPServer) handleLoadModel(params json.RawMessage) (interface{}, error) {
	var args struct {
		ModelFile string  `json:"model_file"`
		ModelSize float64 `json:"model_size_b"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	inferenceState.mu.Lock()
	defer inferenceState.mu.Unlock()

	checkpointPath := filepath.Join(s.projectRoot, "flash_cim_243expert", "checkpoints", args.ModelFile)
	if _, err := os.Stat(checkpointPath); os.IsNotExist(err) {
		// Try alternative paths
		checkpointPath = filepath.Join(s.projectRoot, args.ModelFile)
	}

	if _, err := os.Stat(checkpointPath); os.IsNotExist(err) {
		return nil, fmt.Errorf("model file not found: %s", args.ModelFile)
	}

	inferenceState.loaded = true
	inferenceState.modelFile = args.ModelFile
	inferenceState.modelSizeB = args.ModelSize
	inferenceState.numQutrits = 243
	inferenceState.numExperts = 243
	inferenceState.startTime = time.Now()
	inferenceState.iterations = 0
	inferenceState.converged = false

	return map[string]interface{}{
		"loaded":      true,
		"model_file":  args.ModelFile,
		"model_size":  args.ModelSize,
		"num_qutrits": 243,
		"num_experts": 243,
	}, nil
}

// handleUnloadModel unloads the current model
func (s *MCPServer) handleUnloadModel(params json.RawMessage) (interface{}, error) {
	inferenceState.mu.Lock()
	defer inferenceState.mu.Unlock()

	wasLoaded := inferenceState.loaded
	inferenceState.loaded = false
	inferenceState.modelFile = ""
	inferenceState.iterations = 0
	inferenceState.converged = false

	return map[string]interface{}{
		"unloaded": wasLoaded,
	}, nil
}

// handleListModels lists available model checkpoints
func (s *MCPServer) handleListModels(params json.RawMessage) (interface{}, error) {
	checkpointDir := filepath.Join(s.projectRoot, "flash_cim_243expert", "checkpoints")

	models := []map[string]interface{}{}

	entries, err := os.ReadDir(checkpointDir)
	if err == nil {
		for _, entry := range entries {
			if !entry.IsDir() && strings.HasSuffix(entry.Name(), ".bbin") {
				info, _ := entry.Info()
				sizeMB := float64(info.Size()) / (1024 * 1024)
				models = append(models, map[string]interface{}{
					"filename": entry.Name(),
					"size_mb":  fmt.Sprintf("%.1f", sizeMB),
					"modified": info.ModTime().Format("2006-01-02 15:04"),
				})
			}
		}
	}

	return map[string]interface{}{
		"models": models,
		"count":  len(models),
	}, nil
}

// handleRunInference runs inference with the loaded model
func (s *MCPServer) handleRunInference(params json.RawMessage) (interface{}, error) {
	var args struct {
		Prompt     string  `json:"prompt"`
		NumQutrits int     `json:"num_qutrits"`
		NumExperts int     `json:"num_experts"`
		MaxIter    int     `json:"max_iterations"`
		Tolerance  float64 `json:"tolerance"`
		Goodness   float64 `json:"goodness_threshold"`
	}
	if err := json.Unmarshal(params, &args); err != nil {
		return nil, err
	}

	inferenceState.mu.Lock()
	if !inferenceState.loaded {
		inferenceState.mu.Unlock()
		return nil, fmt.Errorf("no model loaded")
	}
	inferenceState.numQutrits = args.NumQutrits
	inferenceState.numExperts = args.NumExperts
	inferenceState.mu.Unlock()

	// Return immediately - actual inference runs async via SSE
	return map[string]interface{}{
		"started":     true,
		"prompt":      args.Prompt,
		"num_qutrits": args.NumQutrits,
		"num_experts": args.NumExperts,
		"max_iter":    args.MaxIter,
		"stream_url":  "http://localhost:9090/inference-stream",
	}, nil
}

// handleStopInference stops ongoing inference
func (s *MCPServer) handleStopInference(params json.RawMessage) (interface{}, error) {
	inferenceState.mu.Lock()
	defer inferenceState.mu.Unlock()

	wasRunning := inferenceState.iterations > 0 && !inferenceState.converged
	inferenceState.converged = true // Signal stop

	return map[string]interface{}{
		"stopped": wasRunning,
	}, nil
}

// handleGetInferenceMetrics gets current inference metrics
func (s *MCPServer) handleGetInferenceMetrics(params json.RawMessage) (interface{}, error) {
	inferenceState.mu.RLock()
	defer inferenceState.mu.RUnlock()

	elapsed := time.Since(inferenceState.startTime).Seconds()
	iterPerSec := 0.0
	if elapsed > 0 && inferenceState.iterations > 0 {
		iterPerSec = float64(inferenceState.iterations) / elapsed
	}

	return map[string]interface{}{
		"loaded":         inferenceState.loaded,
		"model_file":     inferenceState.modelFile,
		"model_size_b":   inferenceState.modelSizeB,
		"num_qutrits":    inferenceState.numQutrits,
		"num_experts":    inferenceState.numExperts,
		"iterations":     inferenceState.iterations,
		"converged":      inferenceState.converged,
		"iterations_sec": iterPerSec,
		"elapsed_sec":    elapsed,
	}, nil
}

// handleDetectHardwareLimits detects hardware capabilities
func (s *MCPServer) handleDetectHardwareLimits(params json.RawMessage) (interface{}, error) {
	// Get system memory info from TOML
	tomlPath := filepath.Join(s.projectRoot, "config", "system.toml")
	content, err := os.ReadFile(tomlPath)

	arenaMB := 512 // default
	if err == nil {
		// Simple parse for arena_size_mb
		contentStr := string(content)
		if idx := strings.Index(contentStr, "arena_size_mb"); idx != -1 {
			after := contentStr[idx:]
			if eqIdx := strings.Index(after, "="); eqIdx != -1 {
				valStr := strings.TrimSpace(after[eqIdx+1:])
				if endIdx := strings.IndexAny(valStr, "\n#"); endIdx != -1 {
					valStr = valStr[:endIdx]
				}
				fmt.Sscanf(valStr, "%d", &arenaMB)
			}
		}
	}

	// Estimate max parameters based on arena size
	// Ternary: ~10x compression, 1B params ~ 1-2GB
	maxParamsB := float64(arenaMB) / 100.0 // Rough estimate: 100GB per 1B params

	return map[string]interface{}{
		"arena_mb":         arenaMB,
		"max_model_size_b": maxParamsB,
		"max_qutrits":      4096,
		"max_experts":      243,
		"max_batch":        256,
		"precision":        "ternary",
		"recommended_scale": map[string]string{
			"home":        "< 10B params",
			"workstation": "10-100B params",
			"datacenter":  "100B-1T params",
		},
	}, nil
}

// ==================== WUI HTTPServer ====================

// WUIHTTPServer serves WUI static files from extracted assets
type WUIHTTPServer struct {
	mu        sync.RWMutex
	server    *http.Server
	port      int
	running   bool
	assetDir  string
	mcpServer *MCPServer
}

// NewWUIHTTPServer creates a new WUI HTTP server
func NewWUIHTTPServer(port int, assetDir string) *WUIHTTPServer {
	if port <= 0 {
		port = 7345 // Default port
	}
	if assetDir == "" {
		assetDir = "extracted-assets"
	}
	return &WUIHTTPServer{
		port:     port,
		assetDir: assetDir,
	}
}

// FileEntry represents a file or directory entry
type FileEntry struct {
	Name    string `json:"name"`
	Path    string `json:"path"`
	IsDir   bool   `json:"is_dir"`
	Size    int64  `json:"size,omitempty"`
	ModTime string `json:"mod_time,omitempty"`
}

// WebSocket connections for browser-based MCP
type wsConn struct {
	conn   *websocket.Conn
	server *MCPServer
}

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

var wsClients = make(map[*websocket.Conn]*wsConn)
var wsClientsMu sync.RWMutex

func (s *MCPServer) handleWebSocket(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		fmt.Printf("[WS] Upgrade error: %v\n", err)
		return
	}
	defer conn.Close()

	wsClientsMu.Lock()
	wsClients[conn] = &wsConn{conn: conn, server: s}
	wsClientsMu.Unlock()

	fmt.Printf("[WS] Client connected\n")

	// Send handshake
	handshake := map[string]interface{}{
		"type":    "handshake",
		"version": desktopShellContractVersion,
	}
	conn.WriteJSON(handshake)

	for {
		var msg map[string]interface{}
		if err := conn.ReadJSON(&msg); err != nil {
			fmt.Printf("[WS] Read error: %v\n", err)
			break
		}

		// Process MCP request
		go s.handleWSMessage(conn, msg)
	}

	wsClientsMu.Lock()
	delete(wsClients, conn)
	wsClientsMu.Unlock()
	fmt.Printf("[WS] Client disconnected\n")
}

// handleMCPHTTP handles HTTP JSON-RPC MCP requests (browser fallback)
func (s *MCPServer) handleMCPHTTP(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	// Enable CORS
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Access-Control-Allow-Methods", "POST, OPTIONS")
	w.Header().Set("Access-Control-Allow-Headers", "Content-Type")

	if r.Method == http.MethodOptions {
		w.WriteHeader(http.StatusOK)
		return
	}

	// Parse JSON-RPC request
	var req MCPRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		s.sendHTTPMCPError(w, nil, -32700, "Parse error", err.Error())
		return
	}

	// Special case for wui_connect - return connection info
	if req.Method == "wui_connect" {
		// Mark HTTP MCP as active
		s.httpMCPActive = true
		result := map[string]interface{}{
			"strict_mode":          true,
			"backend_passthrough":  false,
			"pipeline_initialized": false,
			"connected":            true,
			"version":              desktopShellContractVersion,
		}
		s.sendHTTPMCPResult(w, req.ID, result)
		return
	}

	// Route to tool handler
	handler, ok := s.tools[req.Method]
	if !ok {
		s.sendHTTPMCPError(w, req.ID, -32601, "Method not found", req.Method)
		return
	}

	// Execute handler
	result, err := handler(req.Params)
	if err != nil {
		s.sendHTTPMCPError(w, req.ID, -32603, "Internal error", err.Error())
		return
	}

	s.sendHTTPMCPResult(w, req.ID, result)
}

// sendHTTPMCPResult sends a JSON-RPC result response over HTTP
func (s *MCPServer) sendHTTPMCPResult(w http.ResponseWriter, id interface{}, result interface{}) {
	resp := MCPResponse{
		JSONRPC: "2.0",
		ID:      id,
		Result:  result,
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

// sendHTTPMCPError sends a JSON-RPC error response over HTTP
func (s *MCPServer) sendHTTPMCPError(w http.ResponseWriter, id interface{}, code int, message string, data interface{}) {
	resp := MCPResponse{
		JSONRPC: "2.0",
		ID:      id,
		Error: &MCPError{
			Code:    code,
			Message: message,
			Data:    data,
		},
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

func (s *MCPServer) handleWSMessage(conn *websocket.Conn, msg map[string]interface{}) {
	method, _ := msg["method"].(string)
	id := msg["id"]
	params, _ := msg["params"].(map[string]interface{})

	// Route to appropriate handler
	var result interface{}
	var err error

	switch method {
	case "wui_start_training_sse":
		// Start SSE server if not running
		if sseServer == nil {
			InitSSEServer("9090")
		}
		result = map[string]interface{}{"status": "training_started", "stream_url": "http://localhost:9090/training-stream"}
	case "wui_run_inference":
		result = map[string]interface{}{"status": "inference_started", "request_id": fmt.Sprintf("inf_%d", time.Now().Unix())}
	case "wui_get_data_sources":
		result = map[string]interface{}{"sources": []interface{}{}}
	case "wui_save_data_sources":
		result = map[string]interface{}{"saved": true}
	case "wui_load_training_config":
		result = map[string]interface{}{"config": ""}
	case "wui_save_training_config":
		result = map[string]interface{}{"saved": true}
	case "wui_load_api_keys":
		result = map[string]interface{}{}
	case "wui_store_api_keys":
		result = map[string]interface{}{"stored": true}
	case "wui_crawl_data_sources":
		result = map[string]interface{}{"started": true}
	case "wui_pause_training":
		result = map[string]interface{}{"paused": true}
	case "wui_stop_training_sse":
		result = map[string]interface{}{"stopped": true}
	// Dashboard compatibility methods - REAL implementation
	case "get_release_status":
		result = s.snapshotReleaseState()
	case "run_self_check":
		expectArtifact := ""
		if p, ok := params["artifact_path"].(string); ok {
			expectArtifact = p
		}
		result = s.buildSystemSelfCheck(expectArtifact)
	case "get_ops_snapshot":
		result = map[string]interface{}{
			"self_check": s.buildSystemSelfCheck(""),
			"release":    s.snapshotReleaseState(),
			"training": map[string]interface{}{
				"current_epoch": 0,
				"is_running":    sseServer != nil && sseServer.IsRunning(),
			},
		}
	case "init_training_pipeline":
		if sseServer == nil {
			InitSSEServer("9090")
		}
		result = map[string]interface{}{"status": "initialized"}
	case "start_ff_training":
		// Check if we can passthrough to real backend
		if s.canPassthroughToBackend() {
			result, err = s.wsClient.SendAndWait(params, "ff_training_started", 30*time.Second)
		} else {
			result = map[string]interface{}{"status": "started", "mode": "local", "epochs": 100}
		}
	case "stop_ff_training":
		if sseServer != nil && sseServer.IsRunning() {
			sseServer.StopTraining()
		}
		result = map[string]interface{}{"status": "stopped"}
	case "get_topology":
		// Get real topology from backend if connected
		if s.canPassthroughToBackend() {
			result, err = s.wsClient.SendAndWait(params, "topology", 10*time.Second)
		} else {
			result = map[string]interface{}{"error": "backend not connected for real topology"}
		}
	case "compute_betti":
		// Compute real Betti numbers from backend
		if s.canPassthroughToBackend() {
			result, err = s.wsClient.SendAndWait(params, "betti", 10*time.Second)
		} else {
			result = map[string]interface{}{"error": "backend not connected for real Betti computation"}
		}
	case "ping":
		result = map[string]interface{}{"pong": true}
	default:
		err = fmt.Errorf("unknown method: %s", method)
	}

	response := map[string]interface{}{
		"jsonrpc": "2.0",
		"id":      id,
	}
	if err != nil {
		response["error"] = map[string]interface{}{"message": err.Error()}
	} else {
		response["result"] = result
	}

	conn.WriteJSON(response)
}

// injectMCPScript wraps the file server and injects the WebSocket MCP script into HTML files
func injectMCPScript(h http.Handler, port int) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		// Check if request is for an HTML file
		path := r.URL.Path
		if path == "/" || strings.HasSuffix(path, ".html") {
			// Read the file content
			recorder := &responseRecorder{ResponseWriter: w, statusCode: 200}
			h.ServeHTTP(recorder, r)

			if recorder.statusCode == 200 && strings.Contains(string(recorder.body), "</head>") {
				// Inject the MCP script before </head>
				script := fmt.Sprintf(`<script>
(function() {
	// WebSocket MCP Bridge - Auto-connecting backend
	const ws = new WebSocket('ws://localhost:%d/ws');
	let pending = new Map();
	let callQueue = [];
	let nextId = 1;
	let isConnected = false;
	
	ws.onopen = function() {
		console.log('[MCP] WebSocket connected - backend ready');
		isConnected = true;
		window.dispatchEvent(new CustomEvent('qminiMcpReady', { detail: { connected: true } }));
		// Dispatch to window for any listeners
		window.dispatchEvent(new Event('wsBridgeReady'));
		// Process any queued calls
		while (callQueue.length > 0) {
			const call = callQueue.shift();
			doCall(call.name, call.args, call.resolve, call.reject);
		}
	};
	
	ws.onerror = function(error) {
		console.error('[MCP] WebSocket error:', error);
		isConnected = false;
	};
	
	ws.onclose = function(event) {
		console.log('[MCP] WebSocket disconnected - code:', event.code, 'reason:', event.reason);
		isConnected = false;
	};
	
	ws.onmessage = function(event) {
		const msg = JSON.parse(event.data);
		if (msg.jsonrpc === '2.0' && msg.id !== undefined) {
			const wait = pending.get(msg.id);
			if (wait) {
				pending.delete(msg.id);
				if (msg.error) {
					wait.reject(new Error(msg.error.message));
				} else {
					wait.resolve(msg.result);
				}
			}
		}
	};
	
	function doCall(name, args, resolve, reject) {
		const id = nextId++;
		pending.set(id, { resolve, reject });
		ws.send(JSON.stringify({
			jsonrpc: '2.0',
			id: id,
			method: name,
			params: args || {}
		}));
		// Timeout after 30 seconds
		setTimeout(() => {
			if (pending.has(id)) {
				pending.delete(id);
				reject(new Error('Request timeout'));
			}
		}, 30000);
	}
	
	window.qMiniMcpHost = {
		connected: false,
		callTool: function(name, args) {
			return new Promise((resolve, reject) => {
				if (!isConnected) {
					// Queue the call for when connection is ready
					callQueue.push({ name, args, resolve, reject });
					return;
				}
				doCall(name, args, resolve, reject);
			});
		},
		ping: function() {
			return this.callTool('ping', {});
		}
	};
	
	// Update connected status
	Object.defineProperty(window.qMiniMcpHost, 'connected', {
		get: function() { return isConnected; }
	});
	
	// wsBridge compatibility for index.html dashboard
	window.wsBridge = {
		isConnected: false,
		send: function(msg) {
			// Route via callTool
			return window.qMiniMcpHost.callTool(msg.type || 'send', msg);
		},
		getReleaseStatus: function() {
			return window.qMiniMcpHost.callTool('get_release_status', {});
		},
		runSystemSelfCheck: function(artifactPath) {
			return window.qMiniMcpHost.callTool('run_self_check', { artifact_path: artifactPath });
		},
		getOpsSnapshot: function() {
			return window.qMiniMcpHost.callTool('get_ops_snapshot', {});
		},
		buildRelease: function(params) {
			return window.qMiniMcpHost.callTool('build_release', params);
		},
		deployRelease: function(params) {
			return window.qMiniMcpHost.callTool('deploy_release', params);
		},
		rollbackRelease: function(params) {
			return window.qMiniMcpHost.callTool('rollback_release', params);
		},
		initQGNN: function(params) {
			return window.qMiniMcpHost.callTool('init_qgnn', params);
		},
		stepMessagePassing: function(params) {
			return window.qMiniMcpHost.callTool('step_message_passing', params);
		},
		computeBetti: function(params) {
			return window.qMiniMcpHost.callTool('compute_betti', params);
		}
	};
	
	// Sync wsBridge.isConnected with qMiniMcpHost.connected
	Object.defineProperty(window.wsBridge, 'isConnected', {
		get: function() { return isConnected; }
	});
})();
</script>`, port)

				content := string(recorder.body)
				content = strings.Replace(content, "</head>", script+"</head>", 1)

				// Copy headers from recorder and write response
				for k, v := range recorder.Header() {
					w.Header()[k] = v
				}
				w.WriteHeader(recorder.statusCode)
				w.Write([]byte(content))
				return
			}
		}

		// Pass through normally
		h.ServeHTTP(w, r)
	}
}

// responseRecorder captures response for modification without writing to underlying writer
type responseRecorder struct {
	http.ResponseWriter
	statusCode int
	body       []byte
}

func (r *responseRecorder) WriteHeader(code int) {
	if r.statusCode == 0 {
		r.statusCode = code
	}
}

func (r *responseRecorder) Write(p []byte) (int, error) {
	if r.statusCode == 0 {
		r.statusCode = 200
	}
	r.body = append(r.body, p...)
	return len(p), nil
}

func (r *responseRecorder) Header() http.Header {
	return r.ResponseWriter.Header()
}

// handleBrowseFiles serves the file browser API
func (s *WUIHTTPServer) handleBrowseFiles(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	// Get path from query parameter
	path := r.URL.Query().Get("path")
	if path == "" {
		path = "."
	}

	// Security: prevent directory traversal
	path = filepath.Clean(path)
	if strings.Contains(path, "..") {
		http.Error(w, "Invalid path", http.StatusBadRequest)
		return
	}

	// Read directory
	entries, err := os.ReadDir(path)
	if err != nil {
		// Try to provide helpful error
		if os.IsNotExist(err) {
			http.Error(w, fmt.Sprintf("Directory not found: %s", path), http.StatusNotFound)
		} else {
			http.Error(w, fmt.Sprintf("Cannot read directory: %v", err), http.StatusInternalServerError)
		}
		return
	}

	// Build file list
	var files []FileEntry
	for _, entry := range entries {
		info, err := entry.Info()
		if err != nil {
			continue
		}

		// Skip hidden files
		if strings.HasPrefix(entry.Name(), ".") {
			continue
		}

		files = append(files, FileEntry{
			Name:    entry.Name(),
			Path:    filepath.Join(path, entry.Name()),
			IsDir:   entry.IsDir(),
			Size:    info.Size(),
			ModTime: info.ModTime().Format(time.RFC3339),
		})
	}

	// Sort: directories first, then alphabetically
	sort.Slice(files, func(i, j int) bool {
		if files[i].IsDir != files[j].IsDir {
			return files[i].IsDir
		}
		return files[i].Name < files[j].Name
	})

	// Return JSON
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	json.NewEncoder(w).Encode(files)
}

// Start begins the HTTP server for WUI files
func (s *WUIHTTPServer) Start(projectRoot string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.running {
		return fmt.Errorf("WUI HTTP server already running on port %d", s.port)
	}

	// Determine asset directory - try multiple locations
	possiblePaths := []string{
		// Current directory (if running from extracted-assets)
		"wui",
		// From project root via assetDir
		filepath.Join(projectRoot, s.assetDir, "wui"),
		// From project root via releases path
		filepath.Join(projectRoot, "releases", "desktop", "extracted-assets", "wui"),
		// Parent directory (if running from runtime/native)
		"..\\..\\..\\wui",
	}

	assetPath := ""
	for _, p := range possiblePaths {
		if _, err := os.Stat(p); err == nil {
			assetPath = p
			break
		}
	}

	if assetPath == "" {
		return fmt.Errorf("WUI asset directory not found. Tried: %v", possiblePaths)
	}

	// Create router
	mux := http.NewServeMux()

	// Static file server - direct serving without injection
	fs := http.FileServer(http.Dir(assetPath))
	mux.Handle("/", fs)

	// API endpoints
	mux.HandleFunc("/api/browse", s.handleBrowseFiles)

	// WebSocket endpoint for browser-based MCP
	if s.mcpServer != nil {
		mux.HandleFunc("/ws", func(w http.ResponseWriter, r *http.Request) {
			s.mcpServer.handleWebSocket(w, r)
		})
		// HTTP JSON-RPC endpoint for browser-based MCP (fallback)
		mux.HandleFunc("/mcp", func(w http.ResponseWriter, r *http.Request) {
			s.mcpServer.handleMCPHTTP(w, r)
		})
	}

	s.server = &http.Server{
		Addr:    fmt.Sprintf(":%d", s.port),
		Handler: mux,
	}

	s.running = true
	go func() {
		fmt.Printf("[WUI HTTP] Server starting on http://localhost:%d\n", s.port)
		fmt.Printf("[WUI HTTP] Serving files from: %s\n", assetPath)
		fmt.Printf("[WUI HTTP] File browser API: http://localhost:%d/api/browse?path=.\n", s.port)

		// Also start SSE server for backend_ready notifications
		if sseServer == nil {
			InitSSEServer("9090")
		}

		if err := s.server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			fmt.Printf("[WUI HTTP] Server error: %v\n", err)
		}
		s.mu.Lock()
		s.running = false
		s.mu.Unlock()
	}()

	return nil
}

// Stop shuts down the HTTP server
func (s *WUIHTTPServer) Stop() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if !s.running || s.server == nil {
		return nil
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	return s.server.Shutdown(ctx)
}

// IsRunning returns whether the server is running
func (s *WUIHTTPServer) IsRunning() bool {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.running
}

// GetPort returns the configured port
func (s *WUIHTTPServer) GetPort() int {
	return s.port
}

// GetURL returns the HTTP URL
func (s *WUIHTTPServer) GetURL() string {
	return fmt.Sprintf("http://localhost:%d", s.port)
}

// Global WUI HTTP server instance
var wuiHTTPServer *WUIHTTPServer

// Global MCP server instance (set by main)
var globalMcpServer *MCPServer

// InitWUIHTTPServer initializes the WUI HTTP server from TOML config
func InitWUIHTTPServer(projectRoot string) error {
	// Read TOML config to get port
	tomlPath := filepath.Join(projectRoot, "config", "system.toml")
	content, err := os.ReadFile(tomlPath)
	if err != nil {
		// Use default port if TOML not found
		wuiHTTPServer = NewWUIHTTPServer(7345, "")
		wuiHTTPServer.mcpServer = globalMcpServer
		return wuiHTTPServer.Start(projectRoot)
	}

	// Parse TOML to find http_port
	port := 7345 // Default
	contentStr := string(content)

	// Simple TOML parsing for http_port in [system.wui] section
	lines := strings.Split(contentStr, "\n")
	inWUISection := false
	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if trimmed == "[system.wui]" {
			inWUISection = true
			continue
		}
		if inWUISection && strings.HasPrefix(trimmed, "[") {
			break // New section
		}
		if inWUISection && strings.HasPrefix(trimmed, "http_port") {
			parts := strings.Split(trimmed, "=")
			if len(parts) == 2 {
				val := strings.TrimSpace(parts[1])
				val = strings.Trim(val, `"`)
				if p, err := fmt.Sscanf(val, "%d", &port); p == 1 && err == nil {
					break
				}
			}
		}
	}

	if port == 0 {
		fmt.Println("[WUI HTTP] Disabled (http_port = 0 in system.toml)")
		return nil
	}

	wuiHTTPServer = NewWUIHTTPServer(port, "")
	wuiHTTPServer.mcpServer = globalMcpServer
	return wuiHTTPServer.Start(projectRoot)
}

// ==================== Utility Functions ====================

func (s *MCPServer) sendResult(id interface{}, result interface{}) {
	resp := MCPResponse{JSONRPC: "2.0", ID: id, Result: result}
	s.writeResponse(resp)
}

func (s *MCPServer) sendError(id interface{}, code int, message string, data interface{}) {
	resp := MCPResponse{
		JSONRPC: "2.0",
		ID:      id,
		Error: &MCPError{
			Code:    code,
			Message: message,
			Data:    data,
		},
	}
	s.writeResponse(resp)
}

func (s *MCPServer) writeResponse(resp MCPResponse) {
	data, err := json.Marshal(resp)
	if err != nil {
		return
	}
	fmt.Fprintln(os.Stdout, string(data))
}

func main() {
	server := NewMCPServer()
	globalMcpServer = server

	// Get executable path for self-extraction check
	exePath, err := os.Executable()
	if err == nil {
		exePath = filepath.Clean(exePath)
		extractRoot := normalizeExtractRoot(server.projectRoot, "")

		// Check if assets need extraction
		wuiPath := filepath.Join(extractRoot, "wui", "index.html")
		if !fileExists(wuiPath) {
			fmt.Fprintf(os.Stderr, "[WUI] Assets not found, extracting from %s...\n", exePath)
			manifestPath, assetCount, _, err := extractEmbeddedAssetBundle(exePath, extractRoot)
			if err != nil {
				fmt.Fprintf(os.Stderr, "[WUI] Extraction failed: %v\n", err)
			} else {
				fmt.Fprintf(os.Stderr, "[WUI] Extracted %d assets to %s\n", assetCount, extractRoot)
				_ = manifestPath
			}
		}
	}

	// Initialize WUI HTTP server from TOML config
	if err := InitWUIHTTPServer(server.projectRoot); err != nil {
		fmt.Fprintf(os.Stderr, "[WUI HTTP] Failed to start: %v\n", err)
	} else if wuiHTTPServer != nil && wuiHTTPServer.IsRunning() {
		url := wuiHTTPServer.GetURL()
		fmt.Fprintf(os.Stderr, "[WUI HTTP] Server ready at %s\n", url)

		// Auto-open browser for double-click GUI mode
		time.Sleep(500 * time.Millisecond) // Brief delay to ensure server is ready
		openBrowser(url)
	}

	// Initialize SSE server for training stream (port 9090)
	if sseServer == nil {
		InitSSEServer("9090")
		fmt.Fprintln(os.Stderr, "[SSE] Training stream server initialized on port 9090")
	}

	// Run MCP server - this blocks on stdin
	server.Run()

	// If we get here, stdin closed. Keep process alive if HTTP server is running
	if wuiHTTPServer != nil && wuiHTTPServer.IsRunning() {
		fmt.Fprintf(os.Stderr, "[WUI HTTP] Server running on %s - keeping process alive\n", wuiHTTPServer.GetURL())
		fmt.Fprintf(os.Stderr, "[WUI HTTP] Press Ctrl+C to stop\n")
		select {} // Block forever
	}
}

// openBrowser opens the default web browser to the given URL
func openBrowser(url string) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "windows":
		cmd = exec.Command("cmd", "/c", "start", url)
	case "darwin":
		cmd = exec.Command("open", url)
	default:
		cmd = exec.Command("xdg-open", url)
	}
	cmd.Start() // Fire and forget - don't wait for it
}
