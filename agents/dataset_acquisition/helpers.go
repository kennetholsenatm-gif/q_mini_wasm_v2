package main

import (
	"bufio"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

type DatasetEntry struct {
	Text       string `json:"text"`
	Source     string `json:"source"`
	Domain     string `json:"domain"`
	URL        string `json:"url,omitempty"`
	RetrievedAt string `json:"retrieved_at"`
}

func NowRFC3339() string {
	return time.Now().UTC().Format(time.RFC3339)
}

func NormalizeText(input string) string {
	if input == "" {
		return ""
	}
	input = strings.ReplaceAll(input, "\r\n", "\n")
	input = strings.ReplaceAll(input, "\t", " ")

	parts := strings.Split(input, "\n")
	clean := make([]string, 0, len(parts))
	for _, part := range parts {
		trimmed := strings.TrimSpace(part)
		if trimmed == "" {
			continue
		}
		clean = append(clean, trimmed)
	}
	return strings.Join(clean, " ")
}

func ExtractUsefulLines(body string, maxLines int, minLen int) []string {
	if maxLines <= 0 {
		maxLines = 256
	}
	if minLen <= 0 {
		minLen = 24
	}

	body = strings.ReplaceAll(body, "\r\n", "\n")
	lines := strings.Split(body, "\n")
	out := make([]string, 0, maxLines)
	seen := make(map[string]struct{})

	for _, raw := range lines {
		line := NormalizeText(raw)
		if len(line) < minLen {
			continue
		}
		if _, exists := seen[line]; exists {
			continue
		}
		seen[line] = struct{}{}
		out = append(out, line)
		if len(out) >= maxLines {
			break
		}
	}

	return out
}

func EnsureDir(path string) error {
	if strings.TrimSpace(path) == "" {
		return fmt.Errorf("empty path")
	}
	return os.MkdirAll(path, 0o755)
}

func CalculateDirSize(path string) error {
	var total int64
	err := filepath.Walk(path, func(_ string, info os.FileInfo, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if info == nil || info.IsDir() {
			return nil
		}
		total += info.Size()
		if total > MaxDiskUsage {
			return fmt.Errorf("disk usage exceeded safety limit: %d bytes > %d bytes", total, MaxDiskUsage)
		}
		return nil
	})
	if os.IsNotExist(err) {
		return nil
	}
	return err
}

func WriteJSONL(path string, entries []DatasetEntry) error {
	if len(entries) == 0 {
		return fmt.Errorf("refusing to write empty dataset to %s", path)
	}
	if err := EnsureDir(filepath.Dir(path)); err != nil {
		return err
	}

	f, err := os.OpenFile(path, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer f.Close()

	for _, entry := range entries {
		entry.Text = NormalizeText(entry.Text)
		if entry.Text == "" {
			continue
		}
		if entry.RetrievedAt == "" {
			entry.RetrievedAt = NowRFC3339()
		}
		b, err := json.Marshal(entry)
		if err != nil {
			return err
		}
		if _, err := f.Write(append(b, '\n')); err != nil {
			return err
		}
	}

	return f.Sync()
}

func CountJSONLLines(path string) (int, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, err
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	buf := make([]byte, 0, 1024*1024)
	scanner.Buffer(buf, 1024*1024)

	count := 0
	for scanner.Scan() {
		if strings.TrimSpace(scanner.Text()) == "" {
			continue
		}
		count++
	}
	if err := scanner.Err(); err != nil {
		return 0, err
	}
	return count, nil
}

func FileSize(path string) (int64, error) {
	info, err := os.Stat(path)
	if err != nil {
		return 0, err
	}
	return info.Size(), nil
}

func FileSHA256(path string) (string, error) {
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

func WriteJSONFile(path string, value interface{}) error {
	if err := EnsureDir(filepath.Dir(path)); err != nil {
		return err
	}
	b, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		return err
	}
	b = append(b, '\n')
	return os.WriteFile(path, b, 0o644)
}

func FetchWikipediaSummary(topic string) (string, string, error) {
	topic = strings.TrimSpace(topic)
	if topic == "" {
		return "", "", fmt.Errorf("topic is required")
	}

	urlValue := "https://en.wikipedia.org/api/rest_v1/page/summary/" + url.PathEscape(strings.ReplaceAll(topic, " ", "_"))
	body, err := FetchURL(urlValue)
	if err != nil {
		return "", urlValue, err
	}

	var payload struct {
		Extract string `json:"extract"`
		Title   string `json:"title"`
	}
	if err := json.Unmarshal(body, &payload); err != nil {
		return "", urlValue, fmt.Errorf("failed to parse wikipedia summary for %s: %w", topic, err)
	}
	if strings.TrimSpace(payload.Extract) == "" {
		return "", urlValue, fmt.Errorf("empty wikipedia extract for %s", topic)
	}

	return NormalizeText(payload.Extract), urlValue, nil
}

type mcpError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

type mcpResponse struct {
	ID     interface{}     `json:"id"`
	Result json.RawMessage `json:"result"`
	Error  *mcpError       `json:"error"`
}

type mcpBridgeClient struct {
	cmd    *exec.Cmd
	stdin  io.WriteCloser
	stdout *bufio.Reader
	nextID int
	mu     sync.Mutex
}

var fetchTransport *mcpBridgeClient

func InitFetchTransport() error {
	if fetchTransport != nil {
		return nil
	}
	client, err := startMCPBridgeClient()
	if err != nil {
		return err
	}
	fetchTransport = client
	return nil
}

func ShutdownFetchTransport() {
	if fetchTransport == nil {
		return
	}
	_ = fetchTransport.Close()
	fetchTransport = nil
}

func FetchURL(target string) ([]byte, error) {
	if fetchTransport == nil {
		return nil, fmt.Errorf("fetch transport not initialized")
	}

	res, err := fetchTransport.CallTool("wui_fetch_url", map[string]interface{}{
		"url":        target,
		"timeout_ms": 60000,
		"max_bytes":  50 * 1024 * 1024,
	})
	if err != nil {
		return nil, err
	}

	body, ok := res["body"].(string)
	if !ok {
		return nil, fmt.Errorf("unexpected fetch response shape for %s", target)
	}

	return []byte(body), nil
}

func startMCPBridgeClient() (*mcpBridgeClient, error) {
	bridgeDir, err := resolveBridgeDirectory()
	if err != nil {
		return nil, err
	}

	binaryPath, err := ensureBridgeBinary(bridgeDir)
	if err != nil {
		return nil, err
	}

	cmd := exec.Command(binaryPath)
	cmd.Dir = bridgeDir
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, err
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		_ = stdin.Close()
		return nil, err
	}
	cmd.Stderr = os.Stderr

	if err := cmd.Start(); err != nil {
		_ = stdin.Close()
		return nil, err
	}

	client := &mcpBridgeClient{
		cmd:    cmd,
		stdin:  stdin,
		stdout: bufio.NewReaderSize(stdout, 1024*1024),
		nextID: 1,
	}

	if _, err := client.call("initialize", map[string]interface{}{
		"protocolVersion": "2024-11-05",
		"capabilities":    map[string]interface{}{},
		"clientInfo": map[string]interface{}{
			"name":    "dataset-acquisition",
			"version": "1.0.0",
		},
	}); err != nil {
		_ = client.Close()
		return nil, fmt.Errorf("failed to initialize MCP bridge: %w", err)
	}

	_ = client.notify("initialized", map[string]interface{}{})
	return client, nil
}

func resolveBridgeDirectory() (string, error) {
	if envPath := strings.TrimSpace(os.Getenv("MCP_BRIDGE_BIN")); envPath != "" {
		if _, err := os.Stat(envPath); err == nil {
			return filepath.Dir(envPath), nil
		}
	}

	wd, err := os.Getwd()
	if err != nil {
		return "", err
	}

	candidates := []string{
		filepath.Clean(filepath.Join(wd, "..", "cmd", "wui-cli-bridge")),
		filepath.Clean(filepath.Join(wd, "agents", "cmd", "wui-cli-bridge")),
		filepath.Clean(filepath.Join("agents", "cmd", "wui-cli-bridge")),
	}

	for _, candidate := range candidates {
		if info, statErr := os.Stat(candidate); statErr == nil && info.IsDir() {
			return candidate, nil
		}
	}

	return "", fmt.Errorf("unable to locate agents/cmd/wui-cli-bridge directory")
}

func ensureBridgeBinary(bridgeDir string) (string, error) {
	if envPath := strings.TrimSpace(os.Getenv("MCP_BRIDGE_BIN")); envPath != "" {
		if _, err := os.Stat(envPath); err == nil {
			return filepath.Clean(envPath), nil
		}
	}

	build := exec.Command("go", "build", "-o", "wui-mcp.exe", ".")
	build.Dir = bridgeDir
	build.Env = append(os.Environ(), "CGO_ENABLED=0")
	out, err := build.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("failed to build MCP bridge in %s: %w | %s", bridgeDir, err, string(out))
	}

	binaryPath := filepath.Join(bridgeDir, "wui-mcp.exe")
	if _, err := os.Stat(binaryPath); err != nil {
		return "", fmt.Errorf("bridge binary not found after build: %s", binaryPath)
	}

	return binaryPath, nil
}

func (c *mcpBridgeClient) Close() error {
	if c == nil {
		return nil
	}
	if c.stdin != nil {
		_ = c.stdin.Close()
	}
	if c.cmd != nil && c.cmd.Process != nil {
		_ = c.cmd.Process.Kill()
		_, _ = c.cmd.Process.Wait()
	}
	return nil
}

func (c *mcpBridgeClient) notify(method string, params interface{}) error {
	req := map[string]interface{}{
		"jsonrpc": "2.0",
		"method":  method,
		"params":  params,
	}
	b, err := json.Marshal(req)
	if err != nil {
		return err
	}
	b = append(b, '\n')
	_, err = c.stdin.Write(b)
	return err
}

func normalizeResponseID(v interface{}) (int, bool) {
	switch val := v.(type) {
	case float64:
		return int(val), true
	case int:
		return val, true
	case int32:
		return int(val), true
	case int64:
		return int(val), true
	default:
		return 0, false
	}
}

func (c *mcpBridgeClient) call(method string, params interface{}) (map[string]interface{}, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	id := c.nextID
	c.nextID++

	req := map[string]interface{}{
		"jsonrpc": "2.0",
		"id":      id,
		"method":  method,
		"params":  params,
	}
	b, err := json.Marshal(req)
	if err != nil {
		return nil, err
	}
	b = append(b, '\n')
	if _, err := c.stdin.Write(b); err != nil {
		return nil, err
	}

	for {
		line, err := c.stdout.ReadBytes('\n')
		if err != nil {
			return nil, err
		}
		line = []byte(strings.TrimSpace(string(line)))
		if len(line) == 0 {
			continue
		}

		var resp mcpResponse
		if err := json.Unmarshal(line, &resp); err != nil {
			continue
		}

		respID, ok := normalizeResponseID(resp.ID)
		if !ok || respID != id {
			continue
		}

		if resp.Error != nil {
			return nil, fmt.Errorf("mcp error %d: %s", resp.Error.Code, resp.Error.Message)
		}

		if len(resp.Result) == 0 {
			return map[string]interface{}{}, nil
		}

		result := make(map[string]interface{})
		if err := json.Unmarshal(resp.Result, &result); err != nil {
			return nil, err
		}
		return result, nil
	}
}

func (c *mcpBridgeClient) CallTool(name string, args map[string]interface{}) (map[string]interface{}, error) {
	result, err := c.call("tools/call", map[string]interface{}{
		"name":      name,
		"arguments": args,
	})
	if err != nil {
		return nil, err
	}
	return result, nil
}
