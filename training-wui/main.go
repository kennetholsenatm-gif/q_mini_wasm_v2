// Command training-wui is a small web UI to list training TOML configs and run
// `python -m qminiwasm.engine --config <path>` from the repository root.
package main

import (
	"bufio"
	"bytes"
	"context"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"embed"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

//go:embed web/*
var webFS embed.FS

// embeddedIndexSHA256Hex is the SHA-256 of embedded web/index.html (hex, lowercase). Used to verify the binary matches your checkout.
var embeddedIndexSHA256Hex string

func init() {
	b, err := webFS.ReadFile("web/index.html")
	if err != nil {
		embeddedIndexSHA256Hex = ""
		return
	}
	sum := sha256.Sum256(b)
	embeddedIndexSHA256Hex = hex.EncodeToString(sum[:])
}

var (
	repoRoot          string
	pythonExe         string
	serverAddr        string
	serverAuthToken   string
	agentDebugLogPath string
	runManager        = newManager()
	wsHub             = newRunWSHub()
)

var (
	metricLineRe       = regexp.MustCompile(`epoch=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+mean_loss=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+mean_return=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+mean_mse=([+-]?(?:\d+\.?\d*|\d*\.?\d+))`)
	metricCanonicalRe  = regexp.MustCompile(`qmw_metric\s+epoch=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+mean_loss=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+mean_return=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+mean_mse=([+-]?(?:\d+\.?\d*|\d*\.?\d+))`)
	backendStatusRe    = regexp.MustCompile(`qmw_backend_status\s+backend=(\S+)\s+requested_accelerator=(\S+)\s+selected_device=(\S+)\s+reason_code=(\S+)\s+available=(\d+)\s+support_class=(\S+)\s+device_name=(\S+)\s+strict_xpu=(\d+).*sycl_active=(\d+)\s+sycl_backend=(\S+)\s+sycl_device=(\S+)\s+sycl_fallback=(\S*)\s+sycl_dpctl_count=(\S+)`)
	epochProgressRe    = regexp.MustCompile(`(?i)\bepoch\s+(\d+)\s*/\s*(\d+)\b`)
	pruneStatsRe       = regexp.MustCompile(`(?i)\b(?:base graph|graph)\s*:\s*(\d+)\s*nodes?\s*->\s*pruned\s*to\s*(\d+)\s*nodes?\b`)
	qaoaLoadRe         = regexp.MustCompile(`(?i)\bqaoa_sim_ms=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+qpu_est_ms=([+-]?(?:\d+\.?\d*|\d*\.?\d+))`)
	enclaveTelemetryRe = regexp.MustCompile(
		`qmw_enclave_telemetry estimated_tpem_mb=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+tier_cap_mb=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+enclave_tier=(\S+)\s+use_memory64=(\d+)`,
	)
	routingTelemetryRe = regexp.MustCompile(
		`qmw_routing_telemetry\s+routing_state=(\d+)\s+assignment_ms=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+budget_ms=(\d+)`,
	)
	routingHandoffRe = regexp.MustCompile(
		`qmw_routing_handoff\s+from=(\d+)\s+to=(\d+)\s+assignment_ms=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+budget_ms=(\d+)\s+reason=(\S+)\s+combinatorial_wall=(\d+)`,
	)
	xpuMemTrainingSetupRe = regexp.MustCompile(
		`qmw_xpu_mem\s+phase=training_setup\s+device=(\S+)\s+batch_size=(\d+)\s+train_samples=(\d+)\s+dataloader_workers=(\d+)`,
	)
	xpuMemEpochRe = regexp.MustCompile(
		`qmw_xpu_mem\s+phase=(\S+)\s+epoch=(\d+)\s+epochs_total=(\d+)\s+batches=(\d+)\s+device=xpu:(\d+)\s+allocated_bytes=(-?\d+)\s+max_allocated_bytes=(-?\d+)\s+allocated_mib=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+max_allocated_mib=([+-]?(?:\d+\.?\d*|\d*\.?\d+))`,
	)
	trainThroughputRe = regexp.MustCompile(
		`qmw_train_throughput\s+epoch=(\d+)\s+epochs_total=(\d+)\s+wall_s=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+batches=(\d+)\s+samples=(\d+)\s+batch_size=(\d+)\s+samples_per_s=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+batches_per_s=([+-]?(?:\d+\.?\d*|\d*\.?\d+))\s+dataloader_workers=(\d+)\s+cascade_s=([+-]?(?:\d+\.?\d*|\d*\.?\d+))(?:\s+host_rss_mib=([+-]?(?:\d+\.?\d*|\d*\.?\d+)))?`,
	)
	alertXPU  = "ACCELERATOR=xpu but PyTorch XPU is not available"
	alertWASM = "switching to mock WASM mode"

	// WebSocket metric payloads include telemetry_source for Mission Control filtering.
	telemetrySourcePythonVNV = "Source: Python VNV"
	telemetrySourceGRPCCPP   = "Source: gRPC C++"
)

// wuiWorkingConfigRel is the single TOML path the WUI writes from Build wizard and Configuration
// schema form. Training is started only via POST /api/runs with this path (or another file) selected.
const wuiWorkingConfigRel = "configs/training/wui_working.toml"

// artifactModelDir returns repo-relative path artifacts/models/<stem>/ (forward slashes).
func artifactModelDir(stem string) string {
	return filepath.ToSlash(filepath.Join("artifacts", "models", stem))
}

func checkpointPathsInModelDir(stem string) (finalRel, bestRel, latestRel string) {
	base := artifactModelDir(stem)
	finalRel = filepath.ToSlash(filepath.Join(base, "final.pt"))
	bestRel = filepath.ToSlash(filepath.Join(base, "best.pt"))
	latestRel = filepath.ToSlash(filepath.Join(base, "latest.pt"))
	return finalRel, bestRel, latestRel
}

// isHFMultiPrimaryPlaceholder is true when data.path means "do not load a primary Hub dataset;
// use only [huggingface].extra_specs" (multi-dataset / extras-only). Case-insensitive.
func isHFMultiPrimaryPlaceholder(dataPath string) bool {
	s := strings.ToLower(strings.TrimSpace(dataPath))
	return s == "qminiwasm/hf-multi" || s == "qminiwasm/multi"
}

func countNonEmptyHFExtraSpecs(specs []hfExtraSpec) int {
	n := 0
	for _, ex := range specs {
		if strings.TrimSpace(ex.Path) != "" {
			n++
		}
	}
	return n
}

// parseExtraSpecsFromValues reads Dataset Builder / WUI payload under "huggingface.extra_specs"
// (JSON string or JSON array from the client).
func parseExtraSpecsFromValues(values map[string]any) []hfExtraSpec {
	raw, ok := values["huggingface.extra_specs"]
	if !ok || raw == nil {
		return nil
	}
	switch v := raw.(type) {
	case string:
		s := strings.TrimSpace(v)
		if s == "" || s == "null" {
			return nil
		}
		var out []hfExtraSpec
		if err := json.Unmarshal([]byte(s), &out); err != nil {
			return nil
		}
		return trimHFExtraSpecs(out)
	case []any:
		var out []hfExtraSpec
		for _, item := range v {
			m, ok := item.(map[string]any)
			if !ok {
				continue
			}
			p, _ := m["path"].(string)
			if strings.TrimSpace(p) == "" {
				continue
			}
			dc, _ := m["dataset_config"].(string)
			out = append(out, hfExtraSpec{Path: strings.TrimSpace(p), DatasetConfig: strings.TrimSpace(dc)})
		}
		return trimHFExtraSpecs(out)
	default:
		return nil
	}
}

func trimHFExtraSpecs(in []hfExtraSpec) []hfExtraSpec {
	seen := make(map[string]struct{})
	var out []hfExtraSpec
	for _, ex := range in {
		p := strings.TrimSpace(ex.Path)
		if p == "" {
			continue
		}
		c := strings.TrimSpace(ex.DatasetConfig)
		key := p + "\x00" + c
		if _, dup := seen[key]; dup {
			continue
		}
		seen[key] = struct{}{}
		out = append(out, hfExtraSpec{Path: p, DatasetConfig: c})
	}
	return out
}

func rawDataPathFromSchemaValues(values map[string]any) string {
	v, ok := values["data.path"]
	if !ok || v == nil {
		return ""
	}
	return strings.TrimSpace(fmt.Sprintf("%v", v))
}

func buildServeToml(stem string) string {
	bestRel := filepath.ToSlash(filepath.Join(artifactModelDir(stem), "best.pt"))
	serveSelf := filepath.ToSlash(filepath.Join(artifactModelDir(stem), "serve.toml"))
	var b strings.Builder
	b.WriteString("# Auto-generated by training-wui. From repository root:\n")
	b.WriteString("#   QMINIWASM_SERVE_CONFIG=" + serveSelf + "\n")
	b.WriteString("# Or set QMINIWASM_CHECKPOINT=" + bestRel + "\n")
	b.WriteString("# HTTP API: GET /health, POST /infer (see agent_bundle.json)\n\n")
	b.WriteString("[serve]\n")
	b.WriteString("checkpoint = " + strconv.Quote(bestRel) + "\n")
	b.WriteString("hybrid_adapter = false\n")
	b.WriteString("hybrid_adapter_hidden = 1024\n")
	b.WriteString("use_cascade_router = true\n")
	b.WriteString("cascade_state_dim = 8\n")
	b.WriteString("cascade_num_actions = 4\n")
	b.WriteString("cascade_router_hidden = 32\n")
	return b.String()
}

func buildAgentBundleJSON(stem, trainingConfigRel, finalRel, bestRel, latestRel string) ([]byte, error) {
	bundleRel := filepath.ToSlash(filepath.Join(artifactModelDir(stem), "agent_bundle.json"))
	serveRel := filepath.ToSlash(filepath.Join(artifactModelDir(stem), "serve.toml"))
	m := map[string]any{
		"schema_version": 1,
		"model_stem":     stem,
		"generated_by":   "training-wui",
		"generated_at":   time.Now().UTC().Format(time.RFC3339),
		"paths": map[string]any{
			"training_config": trainingConfigRel,
			"repo_relative": map[string]string{
				"checkpoint_final":  finalRel,
				"checkpoint_best":   bestRel,
				"checkpoint_latest": latestRel,
				"serve_config":      serveRel,
				"agent_bundle":      bundleRel,
			},
		},
		"inference": map[string]any{
			"recommended_checkpoint": "best",
			"serve": map[string]string{
				"command":                    "uvicorn qminiwasm.engine.serve:app --host 0.0.0.0 --port 8001",
				"env_qminiwasm_serve_config": "QMINIWASM_SERVE_CONFIG=" + serveRel,
				"env_qminiwasm_checkpoint":   "QMINIWASM_CHECKPOINT=" + bestRel,
			},
			"http": map[string]string{
				"health": "GET /health",
				"infer":  "POST /infer JSON: { \"hidden_states\": [[...4096 floats per row...]] }",
			},
		},
	}
	return json.MarshalIndent(m, "", "  ")
}

func writeAgentDeploymentFiles(repoRoot, stem, trainingConfigRel string) error {
	dir := filepath.Join(repoRoot, filepath.FromSlash(artifactModelDir(stem)))
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	finalRel, bestRel, latestRel := checkpointPathsInModelDir(stem)
	servePath := filepath.Join(dir, "serve.toml")
	if err := os.WriteFile(servePath, []byte(buildServeToml(stem)), 0o644); err != nil {
		return err
	}
	bundlePath := filepath.Join(dir, "agent_bundle.json")
	data, err := buildAgentBundleJSON(stem, trainingConfigRel, finalRel, bestRel, latestRel)
	if err != nil {
		return err
	}
	return os.WriteFile(bundlePath, data, 0o644)
}

func main() {
	addr := flag.String("addr", ":8765", "HTTP listen address (host:port)")
	root := flag.String("root", ".", "repository root")
	py := flag.String("python", "python", "Python executable name or path on PATH")
	token := flag.String("token", "", "If set, require this token (Bearer / X-QMW-WUI-Token / ?wui_token=) on all routes including WebSocket")
	strictAddr := flag.Bool("strict-addr", false, "Exit if -addr cannot be bound; do not try the next port (avoids a stale UI when an old process still holds the default port)")
	wuiConfigPath := flag.String("wui-config", "", "Path to WUI settings TOML (default: configs/wui.toml under -root)")
	trainingRuntimeFlag := flag.String("training-runtime", "", "Override [wui] training_runtime_mode from TOML (native|grpc|cpp|auto|python|optimized)")
	grpcAddrFlag := flag.String("grpc-addr", "", "Override [wui] grpc_addr (host:port for C++ TrainingEngineService)")
	flag.Parse()
	// Go's log defaults to stderr; Windows PowerShell treats native stderr as ErrorRecord
	// (NativeCommandError) even for informational lines—use stdout for operator messages.
	log.SetOutput(os.Stdout)
	serverAuthToken = strings.TrimSpace(*token)

	abs, err := filepath.Abs(*root)
	if err != nil {
		log.Fatal(err)
	}
	repoRoot = filepath.Clean(abs)

	// Load repo .env (e.g. /opt/qmw/.env from bind mount) so IBM/HF tokens are visible
	// to this process and subprocesses (python -m qminiwasm.engine).
	loadDotenvFromRepo(repoRoot)

	pythonExe = resolvePythonExecutable(*py)

	trainingDir := filepath.Join(repoRoot, "configs", "training")
	if st, err := os.Stat(trainingDir); err != nil || !st.IsDir() {
		parent := filepath.Clean(filepath.Join(repoRoot, ".."))
		parentTrainingDir := filepath.Join(parent, "configs", "training")
		if pst, perr := os.Stat(parentTrainingDir); perr == nil && pst.IsDir() {
			log.Printf("info: auto-detected repo root at parent: %s", parent)
			repoRoot = parent
			trainingDir = parentTrainingDir
		} else {
			log.Printf("warning: %q missing or not a directory (set -root to repo root)", trainingDir)
		}
	}

	var modeOverride, grpcOverride *string
	if s := strings.TrimSpace(*trainingRuntimeFlag); s != "" {
		modeOverride = trainingRuntimeFlag
	}
	if s := strings.TrimSpace(*grpcAddrFlag); s != "" {
		grpcOverride = grpcAddrFlag
	}
	if err := loadWUIConfig(repoRoot, *wuiConfigPath, modeOverride, grpcOverride); err != nil {
		log.Fatal(err)
	}

	agentDebugLogPath = filepath.Join(repoRoot, "training-wui", "debug-4b1a8d.log")
	if st, err := os.Stat(filepath.Join(repoRoot, "training-wui")); err != nil || !st.IsDir() {
		agentDebugLogPath = filepath.Join(repoRoot, "debug-4b1a8d.log")
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/api/configs", handleConfigs)
	mux.HandleFunc("/api/preflight", handlePreflight)
	mux.HandleFunc("/api/model/facts", handleModelFacts)
	mux.HandleFunc("/api/lr/auto", handleAutoLR)
	mux.HandleFunc("/api/runs", handleRunsCollection)
	mux.HandleFunc("/api/training/start", handleTrainingStart)
	mux.HandleFunc("/api/runs/build", handleRunBuild)
	mux.HandleFunc("/api/runs/custom", handleRunCustom)
	mux.HandleFunc("/api/schema", handleSchema)
	mux.HandleFunc("/api/meta", handleMeta)
	mux.HandleFunc("/api/artifacts", handleArtifactsList)
	mux.HandleFunc("/api/artifacts/download", handleArtifactDownload)
	mux.HandleFunc("/api/artifacts/push_hf", handleArtifactsPushHF)
	mux.HandleFunc("/api/build_artifact", handleBuildArtifact)
	mux.HandleFunc("/api/edge_artifacts/download", handleEdgeArtifactDownload)
	mux.HandleFunc("/api/node/health", handleNodeHealth)
	mux.HandleFunc("/api/quantum/topology", handleQuantumTopology)
	mux.HandleFunc("/api/runpod/status", handleRunpodStatus)
	mux.HandleFunc("/api/runpod/tofu", handleRunpodTofu)
	mux.HandleFunc("/api/runpod/tfvars", handleRunpodTfvars)
	mux.HandleFunc("/api/runpod/warm-targets", handleRunpodWarmTargets)
	mux.HandleFunc("/api/serve/start", handleServeStart)
	mux.HandleFunc("/api/serve/stop", handleServeStop)
	mux.HandleFunc("/api/serve/status", handleServeStatus)
	mux.HandleFunc("/api/runpod/serverless/meta", handleRunpodServerlessMeta)
	mux.HandleFunc("/api/runpod/serverless/endpoints", handleRunpodServerlessEndpoints)
	mux.HandleFunc("/api/runpod/serverless/templates", handleRunpodServerlessTemplates)
	mux.HandleFunc("/api/runpod/serverless/health", handleRunpodServerlessHealth)
	mux.HandleFunc("/api/runpod/serverless/worker-image", handleRunpodServerlessWorkerImage)
	mux.HandleFunc("/api/runpod/serverless/run", handleRunpodServerlessRun)
	mux.HandleFunc("/api/runpod/serverless/job", handleRunpodServerlessJob)
	mux.HandleFunc("/api/runs/", handleRunsItem)

	sub, err := fs.Sub(webFS, "web")
	if err != nil {
		log.Fatal(err)
	}
	mux.Handle("/", noCache(http.FileServer(http.FS(sub))))

	maxPortTries := 10
	if *strictAddr {
		maxPortTries = 1
	}
	actualAddr, ln, err := listenWithPortFallback(*addr, maxPortTries)
	if err != nil {
		log.Fatal(err)
	}
	serverAddr = actualAddr
	log.Printf("training-wui listening on %s (repo root %s, python %q)", actualAddr, repoRoot, pythonExe)
	log.Printf("training-wui UI: %s", wuiBrowseURL(actualAddr))
	if embeddedIndexSHA256Hex != "" {
		log.Printf("embedded web/index.html sha256=%s (GET /api/meta -> embedded_web_index_sha256)", embeddedIndexSHA256Hex)
	} else {
		log.Printf("warning: embedded web/index.html missing from binary")
	}
	// #region agent log
	{
		ln, _ := json.Marshal(map[string]any{
			"sessionId":    "4b1a8d",
			"hypothesisId": "D",
			"location":     "main.go:after_listen",
			"message":      "server_bind_ok",
			"data": map[string]any{
				"actualAddr": actualAddr,
				"go_os":      runtime.GOOS,
			},
			"timestamp": time.Now().UnixMilli(),
		})
		if f, err := os.OpenFile(agentDebugLogPath, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644); err == nil {
			_, _ = f.Write(append(ln, '\n'))
			_ = f.Close()
		}
	}
	// #endregion agent log
	stack := http.Handler(mux)
	if serverAuthToken != "" {
		stack = withAuth(stack)
	}
	log.Fatal(http.Serve(ln, withCORS(stack)))
}

func resolvePythonExecutable(preferred string) string {
	candidates := make([]string, 0, 5)
	seen := map[string]bool{}
	add := func(v string) {
		v = strings.TrimSpace(v)
		if v == "" || seen[v] {
			return
		}
		seen[v] = true
		candidates = append(candidates, v)
	}

	add(preferred)
	add("python")
	add("python3")

	for _, c := range candidates {
		if _, err := exec.LookPath(c); err == nil {
			return c
		}
	}
	return preferred
}

func handleMeta(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"server_addr":                 serverAddr,
		"repo_root":                   repoRoot,
		"python":                      pythonExe,
		"embedded_web_index_sha256":   embeddedIndexSHA256Hex,
		"runtime_profile": map[string]any{
			"default_profile":       "optimized_auto",
			"wui_config_path":       wuiResolved.ConfigPath,
			"training_runtime_mode": trainingRuntimeModeRaw(),
			"native_strict_enabled": wuiResolved.RuntimeProfile.NativeStrictEnabled,
			"ternary_impl":          wuiResolved.RuntimeProfile.TernaryImpl,
			"trit_pack_impl":        wuiResolved.RuntimeProfile.TritPackImpl,
			"memory_encode_impl":    wuiResolved.RuntimeProfile.MemoryEncodeImpl,
			"wasm_exec_impl":        wuiResolved.RuntimeProfile.WasmExecImpl,
			"cascade_rl_impl":       wuiResolved.RuntimeProfile.CascadeRLImpl,
			"tpem_native_bundle":    wuiResolved.RuntimeProfile.TPEMNativeBundle,
			"grpc_engine_reachable": trainingGRPCReachable(600 * time.Millisecond),
			"grpc_engine_addr":      trainingGRPCAddressResolved(),
		},
		"runpod_remote": map[string]any{
			"ssh_user":    runpodSSHUser(),
			"remote_dir":  runpodRemoteDir(),
			"ssh_key_set": runpodSSHKeyPath() != "",
			"ssh":         runpodToolOK("ssh"),
			"scp":         runpodToolOK("scp"),
			"tar":         runpodToolOK("tar"),
		},
		"runpod_serverless": map[string]any{
			"endpoint_configured":       runpodServerlessEndpointID() != "",
			"token_present":             runpodServerlessQueueAPIKey() != "",
			"endpoint_key_present":      strings.TrimSpace(os.Getenv("RUNPOD_TOKEN_END")) != "",
			"management_key_present":    runpodAccountAPIKeyForREST() != "",
			"api_base":                  "https://api.runpod.ai/v2",
			"docs":                      "https://docs.runpod.io/serverless/overview",
			"default_worker_image":      runpodServerlessDefaultWorkerImage(),
			"default_container_disk_gb": runpodServerlessDefaultContainerDiskGB(),
		},
	})
}

// wuiBrowseURL returns a stable http URL operators can open in a browser (127.0.0.1 for wildcard binds).
func wuiBrowseURL(listenHostPort string) string {
	host, port, err := net.SplitHostPort(listenHostPort)
	if err != nil || port == "" {
		return "http://127.0.0.1:8765/"
	}
	hnorm := host
	if strings.HasPrefix(hnorm, "[") && strings.HasSuffix(hnorm, "]") {
		hnorm = hnorm[1 : len(hnorm)-1]
	}
	if hnorm == "" || hnorm == "0.0.0.0" || hnorm == "::" {
		return fmt.Sprintf("http://127.0.0.1:%s/", port)
	}
	if strings.Contains(hnorm, ":") {
		return fmt.Sprintf("http://[%s]:%s/", hnorm, port)
	}
	return fmt.Sprintf("http://%s:%s/", hnorm, port)
}

func listenWithPortFallback(addr string, maxAttempts int) (string, net.Listener, error) {
	addr = strings.TrimSpace(addr)
	if addr == "" {
		addr = ":8765"
	}
	if maxAttempts < 1 {
		maxAttempts = 1
	}
	host, port, err := net.SplitHostPort(addr)
	if err != nil {
		ln, lerr := net.Listen("tcp", addr)
		if lerr != nil {
			return "", nil, lerr
		}
		return ln.Addr().String(), ln, nil
	}
	basePort, err := strconv.Atoi(port)
	if err != nil {
		ln, lerr := net.Listen("tcp", addr)
		if lerr != nil {
			return "", nil, lerr
		}
		return ln.Addr().String(), ln, nil
	}
	var lastErr error
	for i := 0; i < maxAttempts; i++ {
		tryPort := strconv.Itoa(basePort + i)
		tryAddr := net.JoinHostPort(host, tryPort)
		ln, lerr := net.Listen("tcp", tryAddr)
		if lerr == nil {
			if i > 0 {
				log.Printf("warning: requested listen address %q was busy; bound to %q instead", addr, tryAddr)
				log.Printf("warning: open the Training WUI at %s - traffic on the original port may still be an older process (stale UI)", wuiBrowseURL(tryAddr))
			}
			return tryAddr, ln, nil
		}
		lastErr = lerr
	}
	return "", nil, lastErr
}

func withCORS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization, X-QMW-WUI-Token, X-QMW-Source")
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next.ServeHTTP(w, r)
	})
}

func authTokenFromRequest(r *http.Request) string {
	authz := strings.TrimSpace(r.Header.Get("Authorization"))
	if strings.HasPrefix(strings.ToLower(authz), "bearer ") {
		return strings.TrimSpace(authz[7:])
	}
	if v := strings.TrimSpace(r.Header.Get("X-QMW-WUI-Token")); v != "" {
		return v
	}
	return strings.TrimSpace(r.URL.Query().Get("wui_token"))
}

func authOK(r *http.Request) bool {
	if serverAuthToken == "" {
		return true
	}
	got := authTokenFromRequest(r)
	if got == "" {
		return false
	}
	return subtleConstantTimeEqual(got, serverAuthToken)
}

func subtleConstantTimeEqual(a, b string) bool {
	aa, bb := []byte(a), []byte(b)
	if len(aa) != len(bb) {
		return false
	}
	return subtle.ConstantTimeCompare(aa, bb) == 1
}

func withAuth(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !authOK(r) {
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusUnauthorized)
			_ = json.NewEncoder(w).Encode(map[string]string{"error": "unauthorized: pass Authorization Bearer, X-QMW-WUI-Token, or ?wui_token="})
			return
		}
		next.ServeHTTP(w, r)
	})
}

func noCache(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-store, no-cache, must-revalidate, proxy-revalidate")
		w.Header().Set("Pragma", "no-cache")
		w.Header().Set("Expires", "0")
		next.ServeHTTP(w, r)
	})
}

func handleConfigs(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	dir := filepath.Join(repoRoot, "configs", "training")
	entries, err := os.ReadDir(dir)
	if err != nil {
		jsonErr(w, http.StatusInternalServerError, err.Error())
		return
	}
	type item struct {
		Name string `json:"name"`
		Path string `json:"path"`
	}
	var out []item
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		if strings.ToLower(filepath.Ext(e.Name())) != ".toml" {
			continue
		}
		if strings.EqualFold(e.Name(), "schema.toml") {
			continue // reference-only template
		}
		rel := filepath.ToSlash(filepath.Join("configs", "training", e.Name()))
		out = append(out, item{Name: e.Name(), Path: rel})
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"configs": out})
}

// TrainingRequest is the JSON body for POST /api/runs.
type TrainingRequest struct {
	Config                     string `json:"config"`
	QuantumBackend             string `json:"quantum_backend"`
	QuantumPolicy              string `json:"quantum_policy"`
	IBMBackendName             string `json:"ibm_backend_name"`
	RunTarget                  string `json:"run_target"`
	RunpodDestroyOnExit        *bool  `json:"runpod_destroy_on_exit"`
	RunpodVarFile              string `json:"runpod_var_file"`
	RunpodSkipApply            *bool  `json:"runpod_skip_apply"`
	RunpodTrainOnPod           *bool  `json:"runpod_train_on_pod"`
	RunpodServerlessEndpointID string `json:"runpod_serverless_endpoint_id"`
	RunpodWarmTargetID         string `json:"runpod_warm_target_id"`
	AllowMissingCheckpoint     bool   `json:"allow_missing_checkpoint"`
	EnclaveTier                int    `json:"enclave_tier"`
	MemoryLimitMB              int    `json:"memory_limit_mb"`
	HardwareAccelerator        string `json:"hardware_accelerator"`
}

func (r *TrainingRequest) validateEdgeFields() error {
	if r == nil {
		return errors.New("nil request")
	}
	if r.EnclaveTier < 0 || r.EnclaveTier > 5 {
		return errors.New("enclave_tier must be between 0 and 5")
	}
	if r.MemoryLimitMB < 0 {
		return errors.New("memory_limit_mb must be >= 0")
	}
	a := strings.ToLower(strings.TrimSpace(r.HardwareAccelerator))
	if a == "" {
		return nil
	}
	switch a {
	case "cpu", "cuda", "xpu", "sycl", "quantum_mesh":
		return nil
	default:
		return errors.New("hardware_accelerator must be cpu, cuda, xpu, sycl, or quantum_mesh")
	}
}

// BuildArtifactRequest is the JSON body for POST /api/build_artifact.
type BuildArtifactRequest struct {
	WeightsPath string `json:"weights_path"`
	EnclaveTier int    `json:"enclave_tier"`
	OutDir      string `json:"out_dir"` // optional repo-relative directory
}

func handleBuildArtifact(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var body BuildArtifactRequest
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonErr(w, http.StatusBadRequest, "invalid JSON")
		return
	}
	wp := strings.TrimSpace(body.WeightsPath)
	if wp == "" {
		jsonErr(w, http.StatusBadRequest, "weights_path is required")
		return
	}
	if body.EnclaveTier < 0 || body.EnclaveTier > 5 {
		jsonErr(w, http.StatusBadRequest, "enclave_tier must be between 0 and 5")
		return
	}
	tier := body.EnclaveTier
	if tier == 0 {
		tier = 2
	}
	absWeights, err := resolveRepoRelativePath(repoRoot, wp)
	if err != nil {
		jsonErr(w, http.StatusBadRequest, err.Error())
		return
	}
	if st, err := os.Stat(absWeights); err != nil || st.IsDir() {
		jsonErr(w, http.StatusBadRequest, "weights file not found")
		return
	}

	var outAbs string
	if strings.TrimSpace(body.OutDir) != "" {
		outAbs, err = resolveRepoRelativePath(repoRoot, body.OutDir)
		if err != nil {
			jsonErr(w, http.StatusBadRequest, err.Error())
			return
		}
	} else {
		b := make([]byte, 8)
		_, _ = rand.Read(b)
		sub := filepath.Join("dist", "edge-artifacts", "build-"+hex.EncodeToString(b))
		outAbs = filepath.Join(repoRoot, filepath.FromSlash(sub))
	}
	if err := os.MkdirAll(outAbs, 0o755); err != nil {
		jsonErr(w, http.StatusInternalServerError, "failed to create output directory")
		return
	}

	script := filepath.Join(repoRoot, "scripts", "build_tpem_wasm_artifacts.py")
	if _, err := os.Stat(script); err != nil {
		jsonErr(w, http.StatusInternalServerError, "build script not found")
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 20*time.Minute)
	defer cancel()
	cmd := exec.CommandContext(ctx, pythonExe, script,
		"--tier", strconv.Itoa(tier),
		"--checkpoint", absWeights,
		"--out-dir", outAbs,
	)
	cmd.Dir = repoRoot
	var logBuf bytes.Buffer
	cmd.Stdout = &logBuf
	cmd.Stderr = &logBuf
	if err := cmd.Run(); err != nil {
		msg := strings.TrimSpace(logBuf.String())
		if msg == "" {
			msg = err.Error()
		} else {
			msg = msg + ": " + err.Error()
		}
		jsonErr(w, http.StatusInternalServerError, msg)
		return
	}

	bundle := filepath.Join(outAbs, "qminiwasm-edge-bundle.zip")
	if _, err := os.Stat(bundle); err != nil {
		jsonErr(w, http.StatusInternalServerError, "bundle zip not produced")
		return
	}
	bundleTpem := filepath.Join(outAbs, "qminiwasm-edge-bundle.tpem")
	relOut, _ := filepath.Rel(repoRoot, outAbs)
	relBundle, _ := filepath.Rel(repoRoot, bundle)
	relBundleTpem, _ := filepath.Rel(repoRoot, bundleTpem)
	if _, err := os.Stat(bundleTpem); err != nil {
		relBundleTpem = ""
	}
	relWeights, _ := filepath.Rel(repoRoot, filepath.Join(outAbs, "qminiwasm-weights.tpem"))
	relKernels, _ := filepath.Rel(repoRoot, filepath.Join(outAbs, "qminiwasm-kernels.wasm"))
	relSchema, _ := filepath.Rel(repoRoot, filepath.Join(outAbs, "edge_schema.json"))
	resp := map[string]any{
		"ok":           true,
		"out_dir":      filepath.ToSlash(relOut),
		"bundle_zip":   filepath.ToSlash(relBundle),
		"weights_tpem": filepath.ToSlash(relWeights),
		"kernels_wasm": filepath.ToSlash(relKernels),
		"edge_schema":  filepath.ToSlash(relSchema),
		"enclave_tier": tier,
		"log":          strings.TrimSpace(logBuf.String()),
	}
	if relBundleTpem != "" {
		resp["bundle_tpem"] = filepath.ToSlash(relBundleTpem)
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(resp)
}

// handleTrainingStart is POST-only alias for POST /api/runs (same JSON body).
func handleTrainingStart(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	handleRunsCollection(w, r)
}

func handleRunsCollection(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{"runs": runManager.list()})
	case http.MethodPost:
		var body TrainingRequest
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			jsonErr(w, http.StatusBadRequest, "invalid JSON")
			return
		}
		if err := body.validateEdgeFields(); err != nil {
			jsonErr(w, http.StatusBadRequest, err.Error())
			return
		}
		abs, err := resolveTrainingConfig(repoRoot, body.Config)
		if err != nil {
			jsonErr(w, http.StatusBadRequest, err.Error())
			return
		}
		missing, err := missingLoadCheckpointFromConfig(abs)
		if err != nil {
			jsonErr(w, http.StatusBadRequest, "invalid checkpoint config: "+err.Error())
			return
		}
		// Match engine behavior: missing load_path trains from scratch (strip line for this run).
		// allow_missing_checkpoint is accepted for older clients but no longer required.
		_ = body.AllowMissingCheckpoint
		absForEngine := abs
		var cleanups []string
		removeCleanups := func() {
			for _, p := range cleanups {
				if p != "" {
					_ = os.Remove(p)
				}
			}
		}
		if missing != "" {
			log.Printf("training-wui: checkpoint load_path not found (%s); training from scratch (omit load_path for this run)", missing)
			tmp, werr := writeConfigOmittingCheckpointLoadPath(abs)
			if werr != nil {
				jsonErr(w, http.StatusInternalServerError, "could not prepare config without load_path: "+werr.Error())
				return
			}
			absForEngine = tmp
			cleanups = append(cleanups, tmp)
		}

		overlay, oerr := buildEdgeTomlOverlay(&body, absForEngine)
		if oerr != nil {
			removeCleanups()
			jsonErr(w, http.StatusBadRequest, oerr.Error())
			return
		}
		if len(overlay) > MaxTomlOverlayBytes {
			removeCleanups()
			jsonErr(w, http.StatusBadRequest, "edge profile overlay too large")
			return
		}

		relDisplay := body.Config
		serverlessOverlay := ""
		opts := runStartOptsFromRequest(
			body.RunTarget,
			body.RunpodDestroyOnExit,
			body.RunpodVarFile,
			body.RunpodSkipApply,
			body.RunpodTrainOnPod,
			body.RunpodServerlessEndpointID,
			body.RunpodWarmTargetID,
		)
		rt := normalizeRunTarget(opts.RunTarget)

		if strings.TrimSpace(overlay) != "" {
			if rt == "runpod_serverless" {
				serverlessOverlay = overlay
			} else {
				mergedAbs, mergedRel, werr := writeMergedEdgeTrainingConfig(repoRoot, absForEngine, overlay, newRunID()[:12])
				if werr != nil {
					removeCleanups()
					jsonErr(w, http.StatusInternalServerError, "edge profile merge: "+werr.Error())
					return
				}
				cleanups = append(cleanups, mergedAbs)
				absForEngine = mergedAbs
				relDisplay = mergedRel
			}
		}

		extraEnv := buildQuantumEnvOverrides(body.QuantumBackend, body.QuantumPolicy, body.IBMBackendName)
		extraEnv = append(extraEnv, buildEdgeProfileExtraEnv(&body)...)

		if err := validateRunTarget(opts); err != nil {
			removeCleanups()
			jsonErr(w, http.StatusBadRequest, err.Error())
			return
		}
		run, err := runManager.start(absForEngine, relDisplay, extraEnv, opts, cleanups, serverlessOverlay)
		if err != nil {
			removeCleanups()
			jsonErr(w, http.StatusConflict, err.Error())
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusCreated)
		_ = json.NewEncoder(w).Encode(map[string]any{
			"id":     run.ID,
			"config": run.ConfigRel,
		})
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

type schemaField struct {
	ID          string   `json:"id"`
	Section     string   `json:"section"`
	Key         string   `json:"key"`
	Type        string   `json:"type"`
	Required    bool     `json:"required"`
	Default     string   `json:"default,omitempty"`
	Options     []string `json:"options,omitempty"`
	Description string   `json:"description,omitempty"`
}

func handleSchema(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	templateDefaults := parseSchemaTemplateComments(filepath.Join(repoRoot, "configs", "training", "schema.toml"))
	fields := curatedSchemaFields(templateDefaults)
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"schema_source": "configs/training/schema.toml",
		"fields":        fields,
	})
}

func parseSchemaTemplateComments(path string) map[string]string {
	out := map[string]string{}
	f, err := os.Open(path)
	if err != nil {
		return out
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	section := ""
	for sc.Scan() {
		ln := strings.TrimSpace(sc.Text())
		if strings.HasPrefix(ln, "[") && strings.HasSuffix(ln, "]") {
			section = strings.TrimSpace(strings.TrimSuffix(strings.TrimPrefix(ln, "["), "]"))
			continue
		}
		if !strings.HasPrefix(ln, "#") {
			continue
		}
		ln = strings.TrimSpace(strings.TrimPrefix(ln, "#"))
		if !strings.Contains(ln, "=") {
			continue
		}
		parts := strings.SplitN(ln, "=", 2)
		k := strings.TrimSpace(parts[0])
		v := strings.TrimSpace(parts[1])
		if k == "" || section == "" {
			continue
		}
		id := section + "." + k
		out[id] = strings.TrimSpace(v)
	}
	return out
}

func curatedSchemaFields(defaults map[string]string) []schemaField {
	fields := []schemaField{
		{ID: "hardware.accelerator", Section: "hardware", Key: "accelerator", Type: "enum", Options: []string{"cpu", "cuda", "xpu", "sycl"}, Default: defaults["hardware.accelerator"], Description: "Hardware target for torch device selection."},
		{ID: "hardware.device_index", Section: "hardware", Key: "device_index", Type: "int", Default: defaults["hardware.device_index"]},
		{ID: "hardware.quantum_backend", Section: "hardware", Key: "quantum_backend", Type: "string", Default: defaults["hardware.quantum_backend"]},
		{ID: "hardware.num_qubits", Section: "hardware", Key: "num_qubits", Type: "int", Default: defaults["hardware.num_qubits"]},
		{ID: "hardware.qaoa_layers", Section: "hardware", Key: "qaoa_layers", Type: "int", Default: defaults["hardware.qaoa_layers"]},
		{ID: "hardware.qaoa_execution_mode", Section: "hardware", Key: "qaoa_execution_mode", Type: "enum", Options: []string{"pennylane", "qiskit_statevector", "qiskit_ibm"}, Default: defaults["hardware.qaoa_execution_mode"]},
		{ID: "hardware.ibm_qaoa_shots", Section: "hardware", Key: "ibm_qaoa_shots", Type: "int", Default: defaults["hardware.ibm_qaoa_shots"]},
		{ID: "hardware.diff_method", Section: "hardware", Key: "diff_method", Type: "string", Default: defaults["hardware.diff_method"]},
		{ID: "training.epochs", Section: "training", Key: "epochs", Type: "int", Required: true, Default: defaults["training.epochs"]},
		{ID: "training.batch_size", Section: "training", Key: "batch_size", Type: "int", Required: true, Default: defaults["training.batch_size"]},
		{ID: "training.dataloader_num_workers", Section: "training", Key: "dataloader_num_workers", Type: "int", Default: defaults["training.dataloader_num_workers"], Description: "Host DataLoader workers for supervised batch collation (0 = main process only)."},
		{ID: "training.learning_rate", Section: "training", Key: "learning_rate", Type: "float", Required: true, Default: defaults["training.learning_rate"], Description: "Seed learning rate. Adaptive per-epoch scheduling can update this during training."},
		{ID: "training.seed", Section: "training", Key: "seed", Type: "int", Default: defaults["training.seed"]},
		{ID: "training.grad_clip_norm", Section: "training", Key: "grad_clip_norm", Type: "float", Default: defaults["training.grad_clip_norm"]},
		{ID: "training.lr_plateau_patience", Section: "training", Key: "lr_plateau_patience", Type: "int", Default: defaults["training.lr_plateau_patience"]},
		{ID: "training.lr_plateau_factor", Section: "training", Key: "lr_plateau_factor", Type: "float", Default: defaults["training.lr_plateau_factor"]},
		{ID: "training.lr_plateau_min_lr", Section: "training", Key: "lr_plateau_min_lr", Type: "float", Default: defaults["training.lr_plateau_min_lr"]},
		{ID: "training.early_stop_patience", Section: "training", Key: "early_stop_patience", Type: "int", Default: defaults["training.early_stop_patience"]},
		{ID: "training.log_xpu_memory", Section: "training", Key: "log_xpu_memory", Type: "bool", Default: defaults["training.log_xpu_memory"], Description: "Emit qmw_xpu_mem lines on XPU (saved in working TOML; no env vars)."},
		{ID: "training.log_xpu_memory_reset_peak", Section: "training", Key: "log_xpu_memory_reset_peak", Type: "bool", Default: defaults["training.log_xpu_memory_reset_peak"], Description: "Reset XPU peak memory stats each epoch (with log_xpu_memory)."},
		{ID: "training.log_train_throughput", Section: "training", Key: "log_train_throughput", Type: "bool", Default: defaults["training.log_train_throughput"], Description: "Emit qmw_train_throughput after each supervised phase (wall_s, samples/s, host RSS if psutil)."},
		{ID: "training.auto_stabilize", Section: "training", Key: "auto_stabilize", Type: "bool", Default: defaults["training.auto_stabilize"], Description: "On XPU/SYCL, if grad_clip_norm and lr_plateau_patience are unset, apply clip 1.0 and plateau patience 2."},
		{ID: "data.source", Section: "data", Key: "source", Type: "enum", Options: []string{"mesh", "corpus", "hf_tabular", "hybrid_mesh_hf_tabular"}, Required: true, Default: defaults["data.source"], Description: "Data scheme: mesh generation, Hugging Face tabular, or hybrid blend."},
		{ID: "data.path", Section: "data", Key: "path", Type: "string", Default: defaults["data.path"], Description: "Dataset id/path. For Hugging Face use owner/dataset. Use qminiwasm/hf-multi for extras-only mode."},
		{ID: "data.mesh_algorithms", Section: "data", Key: "mesh_algorithms", Type: "string", Default: defaults["data.mesh_algorithms"]},
		{ID: "huggingface.dataset_config", Section: "huggingface", Key: "dataset_config", Type: "string", Default: defaults["huggingface.dataset_config"]},
		{ID: "huggingface.dataset_revision", Section: "huggingface", Key: "dataset_revision", Type: "string", Default: defaults["huggingface.dataset_revision"], Description: "Hub git ref (branch, tag, or commit) for datasets.load_dataset revision=."},
		{ID: "huggingface.num_samples", Section: "huggingface", Key: "num_samples", Type: "int", Default: defaults["huggingface.num_samples"]},
		{ID: "huggingface.split", Section: "huggingface", Key: "split", Type: "string", Default: defaults["huggingface.split"]},
		{ID: "huggingface.mesh_blend_fraction", Section: "huggingface", Key: "mesh_blend_fraction", Type: "float", Default: defaults["huggingface.mesh_blend_fraction"]},
		{ID: "checkpoint.load_path", Section: "checkpoint", Key: "load_path", Type: "string", Default: defaults["checkpoint.load_path"]},
		{ID: "checkpoint.save_path", Section: "checkpoint", Key: "save_path", Type: "string", Default: defaults["checkpoint.save_path"]},
		{ID: "checkpoint.best_path", Section: "checkpoint", Key: "best_path", Type: "string", Default: defaults["checkpoint.best_path"]},
		{ID: "checkpoint.latest_path", Section: "checkpoint", Key: "latest_path", Type: "string", Default: defaults["checkpoint.latest_path"]},
		{ID: "eval.holdout_fraction", Section: "eval", Key: "holdout_fraction", Type: "float", Default: defaults["eval.holdout_fraction"]},
		{ID: "eval.every_epoch", Section: "eval", Key: "every_epoch", Type: "bool", Default: defaults["eval.every_epoch"]},
		{ID: "eval.early_stop_patience", Section: "eval", Key: "early_stop_patience", Type: "int", Default: defaults["eval.early_stop_patience"], Description: "Holdout eval plateau patience (needs every_epoch + holdout)."},
		{ID: "eval.target_mean_mse", Section: "eval", Key: "target_mean_mse", Type: "float", Default: defaults["eval.target_mean_mse"]},
		{ID: "eval.stop_on_target_mse", Section: "eval", Key: "stop_on_target_mse", Type: "bool", Default: defaults["eval.stop_on_target_mse"]},
		{ID: "adapter.hybrid_adapter", Section: "adapter", Key: "hybrid_adapter", Type: "bool", Default: defaults["adapter.hybrid_adapter"]},
		{ID: "adapter.hybrid_adapter_hidden", Section: "adapter", Key: "hybrid_adapter_hidden", Type: "int", Default: defaults["adapter.hybrid_adapter_hidden"]},
		{ID: "adapter.tequila_deadzone", Section: "adapter", Key: "tequila_deadzone", Type: "float", Default: defaults["adapter.tequila_deadzone"]},
		{ID: "adapter.lota_rank", Section: "adapter", Key: "lota_rank", Type: "int", Default: defaults["adapter.lota_rank"]},
		{ID: "adapter.use_tsign_ternary", Section: "adapter", Key: "use_tsign_ternary", Type: "bool", Default: defaults["adapter.use_tsign_ternary"]},
		{ID: "adapter.tsign_learning_rate", Section: "adapter", Key: "tsign_learning_rate", Type: "float", Default: defaults["adapter.tsign_learning_rate"]},
		{ID: "adapter.lota_merge_every_epoch", Section: "adapter", Key: "lota_merge_every_epoch", Type: "bool", Default: defaults["adapter.lota_merge_every_epoch"]},
		{ID: "cascade.enabled", Section: "cascade", Key: "enabled", Type: "bool", Default: defaults["cascade.enabled"]},
		{ID: "cascade.policy_lr", Section: "cascade", Key: "policy_lr", Type: "float", Default: defaults["cascade.policy_lr"]},
		{ID: "cascade.steps_per_epoch", Section: "cascade", Key: "steps_per_epoch", Type: "int", Default: defaults["cascade.steps_per_epoch"]},
		{ID: "cascade.group_size", Section: "cascade", Key: "group_size", Type: "int", Default: defaults["cascade.group_size"]},
		{ID: "cascade.state_dim", Section: "cascade", Key: "state_dim", Type: "int", Default: defaults["cascade.state_dim"]},
		{ID: "cascade.num_actions", Section: "cascade", Key: "num_actions", Type: "int", Default: defaults["cascade.num_actions"]},
		{ID: "cascade.mopd_lambda", Section: "cascade", Key: "mopd_lambda", Type: "float", Default: defaults["cascade.mopd_lambda"]},
		{ID: "cascade.mopd_feat_loss", Section: "cascade", Key: "mopd_feat_loss", Type: "enum", Options: []string{"mse", "cosine"}, Default: defaults["cascade.mopd_feat_loss"]},
		{ID: "cascade.seed_from_hidden", Section: "cascade", Key: "seed_from_hidden", Type: "bool", Default: defaults["cascade.seed_from_hidden"]},
		{ID: "cascade.use_router", Section: "cascade", Key: "use_router", Type: "bool", Default: defaults["cascade.use_router"]},
		{ID: "cascade.learned_projector", Section: "cascade", Key: "learned_projector", Type: "bool", Default: defaults["cascade.learned_projector"]},
		{ID: "cascade.router_hidden", Section: "cascade", Key: "router_hidden", Type: "int", Default: defaults["cascade.router_hidden"]},
		{ID: "cascade.couple_forward", Section: "cascade", Key: "couple_forward", Type: "bool", Default: defaults["cascade.couple_forward"]},
		{ID: "wasm.store_memory_limit_mb", Section: "wasm", Key: "store_memory_limit_mb", Type: "int", Default: defaults["wasm.store_memory_limit_mb"], Description: "Wasmtime store linear memory cap (MiB). If unset, engine default is ~1e6 × d_model bytes (~4 GiB) for large HF/mesh runs."},
		{ID: "wasm.fallback_policy", Section: "wasm", Key: "fallback_policy", Type: "enum", Options: []string{"mock", "error"}, Default: defaults["wasm.fallback_policy"], Description: "mock: use mock WASM on failure; error: fail fast."},
		{ID: "wasm.force_mock", Section: "wasm", Key: "force_mock", Type: "bool", Default: defaults["wasm.force_mock"], Description: "If true, always use mock WASM (testing / constrained hosts)."},
		{ID: "wasm.store_instance_limit", Section: "wasm", Key: "store_instance_limit", Type: "int", Default: defaults["wasm.store_instance_limit"]},
		{ID: "wasm.store_memories_limit", Section: "wasm", Key: "store_memories_limit", Type: "int", Default: defaults["wasm.store_memories_limit"]},
		{ID: "serve.checkpoint", Section: "serve", Key: "checkpoint", Type: "string", Default: defaults["serve.checkpoint"]},
		{ID: "serve.hybrid_adapter", Section: "serve", Key: "hybrid_adapter", Type: "bool", Default: defaults["serve.hybrid_adapter"]},
		{ID: "serve.hybrid_adapter_hidden", Section: "serve", Key: "hybrid_adapter_hidden", Type: "int", Default: defaults["serve.hybrid_adapter_hidden"]},
		{ID: "serve.use_cascade_router", Section: "serve", Key: "use_cascade_router", Type: "bool", Default: defaults["serve.use_cascade_router"]},
		{ID: "serve.cascade_state_dim", Section: "serve", Key: "cascade_state_dim", Type: "int", Default: defaults["serve.cascade_state_dim"]},
		{ID: "serve.cascade_num_actions", Section: "serve", Key: "cascade_num_actions", Type: "int", Default: defaults["serve.cascade_num_actions"]},
		{ID: "serve.cascade_router_hidden", Section: "serve", Key: "cascade_router_hidden", Type: "int", Default: defaults["serve.cascade_router_hidden"]},
	}
	for i := range fields {
		if fields[i].Default == "" {
			fields[i].Default = ""
		}
	}
	return fields
}

func handleRunCustom(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var req struct {
		Name   string         `json:"name"`
		Values map[string]any `json:"values"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		jsonErr(w, http.StatusBadRequest, "invalid JSON")
		return
	}
	if req.Values == nil {
		req.Values = map[string]any{}
	}
	genDir := filepath.Join(repoRoot, "configs", "training")
	if err := os.MkdirAll(genDir, 0o755); err != nil {
		jsonErr(w, http.StatusInternalServerError, "failed to prepare config directory")
		return
	}
	rel := wuiWorkingConfigRel
	abs := filepath.Join(repoRoot, filepath.FromSlash(rel))
	content, err := buildCustomRunToml(req.Values)
	if err != nil {
		jsonErr(w, http.StatusBadRequest, err.Error())
		return
	}
	if err := os.WriteFile(abs, []byte(content), 0o644); err != nil {
		jsonErr(w, http.StatusInternalServerError, "failed to write generated config")
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	_ = json.NewEncoder(w).Encode(map[string]any{
		"config":  rel,
		"message": "saved working training config (use Launch training to start)",
	})
}

func buildCustomRunToml(values map[string]any) (string, error) {
	fields := curatedSchemaFields(parseSchemaTemplateComments(filepath.Join(repoRoot, "configs", "training", "schema.toml")))
	fieldByID := make(map[string]schemaField, len(fields))
	for _, f := range fields {
		fieldByID[f.ID] = f
	}
	extraSpecs := parseExtraSpecsFromValues(values)
	dpRaw := rawDataPathFromSchemaValues(values)
	if isHFMultiPrimaryPlaceholder(dpRaw) && countNonEmptyHFExtraSpecs(extraSpecs) == 0 {
		return "", fmt.Errorf(
			"data.path is qminiwasm/hf-multi (extras-only Hub mode) but no datasets were configured. " +
				"In the WUI, open Dataset Builder, select Hub datasets, click Apply mix & save working config, then Launch training — " +
				"or add [huggingface].extra_specs in this TOML.",
		)
	}
	sections := map[string]map[string]string{}
	for id, raw := range values {
		fd, ok := fieldByID[id]
		if !ok {
			continue
		}
		if sections[fd.Section] == nil {
			sections[fd.Section] = map[string]string{}
		}
		tv, ok := normalizeSchemaValue(fd, raw)
		if !ok {
			continue
		}
		sections[fd.Section][fd.Key] = tv
	}
	for _, f := range fields {
		if !f.Required {
			continue
		}
		if sections[f.Section] == nil || sections[f.Section][f.Key] == "" {
			return "", fmt.Errorf("missing required field: %s", f.ID)
		}
	}
	if len(extraSpecs) > 0 && sections["huggingface"] == nil {
		sections["huggingface"] = map[string]string{}
	}
	var secNames []string
	for sec := range sections {
		secNames = append(secNames, sec)
	}
	sort.Strings(secNames)
	var b strings.Builder
	b.WriteString("# Generated by training-wui dynamic schema form\n\n")
	for _, sec := range secNames {
		keys := sections[sec]
		if len(keys) == 0 && !(sec == "huggingface" && len(extraSpecs) > 0) {
			continue
		}
		b.WriteString("[" + sec + "]\n")
		if sec == "huggingface" && isHFMultiPrimaryPlaceholder(dpRaw) && len(extraSpecs) > 0 {
			b.WriteString("# data.path is a reserved placeholder; training loads only [huggingface].extra_specs.\n")
		}
		var ks []string
		for k := range keys {
			ks = append(ks, k)
		}
		sort.Strings(ks)
		for _, k := range ks {
			b.WriteString(k + " = " + keys[k] + "\n")
		}
		if sec == "huggingface" && len(extraSpecs) > 0 {
			b.WriteString("extra_specs = [\n")
			for _, ex := range extraSpecs {
				p := strings.TrimSpace(ex.Path)
				if p == "" {
					continue
				}
				b.WriteString("  { path = " + strconv.Quote(p))
				if c := strings.TrimSpace(ex.DatasetConfig); c != "" {
					b.WriteString(", dataset_config = " + strconv.Quote(c))
				}
				b.WriteString(" },\n")
			}
			b.WriteString("]\n")
		}
		b.WriteString("\n")
	}
	return b.String(), nil
}

func normalizeSchemaValue(fd schemaField, raw any) (string, bool) {
	switch fd.Type {
	case "int":
		switch v := raw.(type) {
		case float64:
			return strconv.FormatInt(int64(v), 10), true
		case string:
			v = strings.TrimSpace(v)
			if v == "" {
				return "", false
			}
			n, err := strconv.ParseInt(v, 10, 64)
			if err != nil {
				return "", false
			}
			return strconv.FormatInt(n, 10), true
		}
	case "float":
		switch v := raw.(type) {
		case float64:
			return strconv.FormatFloat(v, 'g', -1, 64), true
		case string:
			v = strings.TrimSpace(v)
			if v == "" {
				return "", false
			}
			n, err := strconv.ParseFloat(v, 64)
			if err != nil {
				return "", false
			}
			return strconv.FormatFloat(n, 'g', -1, 64), true
		}
	case "bool":
		switch v := raw.(type) {
		case bool:
			if v {
				return "true", true
			}
			return "false", true
		case string:
			v = strings.TrimSpace(strings.ToLower(v))
			if v == "true" || v == "false" {
				return v, true
			}
		}
	case "enum", "string":
		s := strings.TrimSpace(fmt.Sprintf("%v", raw))
		if s == "" {
			return "", false
		}
		if fd.Type == "enum" && len(fd.Options) > 0 {
			ok := false
			for _, x := range fd.Options {
				if s == x {
					ok = true
					break
				}
			}
			if !ok {
				return "", false
			}
		}
		return strconv.Quote(s), true
	}
	return "", false
}

func handleArtifactsList(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	base := filepath.Join(repoRoot, "artifacts")
	type item struct {
		Name     string `json:"name"`
		Path     string `json:"path"`
		Size     int64  `json:"size_bytes"`
		Modified string `json:"modified"`
	}
	var out []item
	_ = filepath.WalkDir(base, func(path string, d os.DirEntry, err error) error {
		if err != nil || d == nil || d.IsDir() {
			return nil
		}
		if !strings.EqualFold(filepath.Ext(d.Name()), ".pt") {
			return nil
		}
		st, e := d.Info()
		if e != nil {
			return nil
		}
		rel, e := filepath.Rel(base, path)
		if e != nil {
			return nil
		}
		out = append(out, item{
			Name:     d.Name(),
			Path:     filepath.ToSlash(filepath.Join("artifacts", rel)),
			Size:     st.Size(),
			Modified: st.ModTime().UTC().Format(time.RFC3339),
		})
		return nil
	})
	sort.Slice(out, func(i, j int) bool { return out[i].Modified > out[j].Modified })
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"artifacts": out})
}

func handleArtifactDownload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	rel := strings.TrimSpace(r.URL.Query().Get("path"))
	if rel == "" {
		jsonErr(w, http.StatusBadRequest, "missing path")
		return
	}
	abs := PathJoinRepo(repoRoot, rel)
	base := filepath.Join(repoRoot, "artifacts")
	relBase, err := filepath.Rel(base, abs)
	if err != nil || strings.HasPrefix(relBase, "..") {
		jsonErr(w, http.StatusBadRequest, "invalid artifact path")
		return
	}
	st, err := os.Stat(abs)
	if err != nil || st.IsDir() {
		jsonErr(w, http.StatusNotFound, "artifact not found")
		return
	}
	w.Header().Set("Content-Disposition", `attachment; filename="`+filepath.Base(abs)+`"`)
	if r.Method == http.MethodHead {
		w.Header().Set("Content-Length", strconv.FormatInt(st.Size(), 10))
		w.WriteHeader(http.StatusOK)
		return
	}
	http.ServeFile(w, r, abs)
}

func edgeArtifactsBaseDir(root string) string {
	return filepath.Join(root, "dist", "edge-artifacts")
}

// resolveEdgeArtifactDownloadPath ensures relPath resolves to a file under repoRoot/dist/edge-artifacts.
func resolveEdgeArtifactDownloadPath(root, relPath string) (abs string, err error) {
	relPath = strings.TrimSpace(relPath)
	if relPath == "" {
		return "", errors.New("missing path")
	}
	relPath = strings.TrimPrefix(relPath, "/")
	root = filepath.Clean(root)
	abs = filepath.Join(root, filepath.FromSlash(relPath))
	abs = filepath.Clean(abs)
	base := edgeArtifactsBaseDir(root)
	relBase, e := filepath.Rel(base, abs)
	if e != nil || strings.HasPrefix(relBase, "..") {
		return "", errors.New("path must be under dist/edge-artifacts")
	}
	return abs, nil
}

func handleEdgeArtifactDownload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	rel := strings.TrimSpace(r.URL.Query().Get("path"))
	abs, err := resolveEdgeArtifactDownloadPath(repoRoot, rel)
	if err != nil {
		jsonErr(w, http.StatusBadRequest, err.Error())
		return
	}
	st, err := os.Stat(abs)
	if err != nil || st.IsDir() {
		jsonErr(w, http.StatusNotFound, "edge artifact not found")
		return
	}
	w.Header().Set("Content-Disposition", `attachment; filename="`+filepath.Base(abs)+`"`)
	if r.Method == http.MethodHead {
		w.Header().Set("Content-Length", strconv.FormatInt(st.Size(), 10))
		w.WriteHeader(http.StatusOK)
		return
	}
	http.ServeFile(w, r, abs)
}

func handleArtifactsPushHF(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var body struct {
		Path  string `json:"path"`
		Repo  string `json:"repo"`
		Token string `json:"token"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonErr(w, http.StatusBadRequest, "invalid JSON")
		return
	}
	rel := strings.TrimSpace(body.Path)
	repo := strings.TrimSpace(body.Repo)
	if rel == "" || repo == "" {
		jsonErr(w, http.StatusBadRequest, "path and repo are required")
		return
	}
	abs := PathJoinRepo(repoRoot, rel)
	base := filepath.Join(repoRoot, "artifacts")
	relBase, err := filepath.Rel(base, abs)
	if err != nil || strings.HasPrefix(relBase, "..") {
		jsonErr(w, http.StatusBadRequest, "invalid artifact path")
		return
	}
	if st, err := os.Stat(abs); err != nil || st.IsDir() {
		jsonErr(w, http.StatusNotFound, "artifact not found")
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 2*time.Minute)
	defer cancel()
	script := `
import os
from huggingface_hub import HfApi
path = os.environ["QMW_HF_PATH"]
repo = os.environ["QMW_HF_REPO"]
token = os.environ.get("QMW_HF_TOKEN", "") or None
api = HfApi(token=token)
api.upload_file(
    path_or_fileobj=path,
    path_in_repo=os.path.basename(path),
    repo_id=repo,
    repo_type="model",
)
print("ok")
`
	cmd := exec.CommandContext(ctx, pythonExe, "-c", script)
	cmd.Dir = repoRoot
	cmd.Env = append(
		os.Environ(),
		"QMW_HF_PATH="+abs,
		"QMW_HF_REPO="+repo,
		"QMW_HF_TOKEN="+strings.TrimSpace(body.Token),
	)
	var outb, errb bytes.Buffer
	cmd.Stdout = &outb
	cmd.Stderr = &errb
	if err := cmd.Run(); err != nil {
		msg := strings.TrimSpace(errb.String())
		if msg == "" {
			msg = err.Error()
		}
		jsonErr(w, http.StatusBadRequest, "hf push failed: "+msg)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok":      true,
		"path":    rel,
		"repo":    repo,
		"message": strings.TrimSpace(outb.String()),
	})
}

func handleNodeHealth(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var ms runtime.MemStats
	runtime.ReadMemStats(&ms)
	active := runManager.activeRunSummary()
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"node_health": map[string]any{
			"go_process_alloc_mb": float64(ms.Alloc) / (1024 * 1024),
			"go_process_sys_mb":   float64(ms.Sys) / (1024 * 1024),
			"go_goroutines":       runtime.NumGoroutine(),
			"active_run":          active,
		},
	})
}

func handleQuantumTopology(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	active := runManager.activeRunSummary()
	qt := map[string]any{
		"run_id":              "",
		"pruned_from_nodes":   0,
		"pruned_to_nodes":     0,
		"qaoa_sim_ms":         0.0,
		"qpu_est_ms":          0.0,
		"has_live_topology":   false,
		"routing_matrix_hint": "No live routing matrix emitted yet.",
	}
	if active != nil {
		qt["run_id"] = active["id"]
		qt["pruned_from_nodes"] = active["pruned_from_nodes"]
		qt["pruned_to_nodes"] = active["pruned_to_nodes"]
		qt["qaoa_sim_ms"] = active["qaoa_sim_ms"]
		qt["qpu_est_ms"] = active["qpu_est_ms"]
		hasLive := false
		if v, ok := active["pruned_to_nodes"].(int); ok && v > 0 {
			hasLive = true
		}
		qt["has_live_topology"] = hasLive
		if hasLive {
			qt["routing_matrix_hint"] = "Live QUBO matrix visualization can be enabled from emitted telemetry payloads."
		}
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"quantum_topology": qt})
}

func missingLoadCheckpointFromConfig(configAbs string) (string, error) {
	raw, err := os.ReadFile(configAbs)
	if err != nil {
		return "", err
	}
	lines := strings.Split(string(raw), "\n")
	inCheckpoint := false
	for _, ln := range lines {
		s := strings.TrimSpace(ln)
		if s == "" || strings.HasPrefix(s, "#") {
			continue
		}
		if strings.HasPrefix(s, "[") && strings.HasSuffix(s, "]") {
			inCheckpoint = strings.EqualFold(s, "[checkpoint]")
			continue
		}
		if !inCheckpoint {
			continue
		}
		if !strings.HasPrefix(strings.ToLower(s), "load_path") {
			continue
		}
		parts := strings.SplitN(s, "=", 2)
		if len(parts) != 2 {
			return "", errors.New("checkpoint.load_path must use key = value")
		}
		v := strings.TrimSpace(parts[1])
		v = strings.SplitN(v, "#", 2)[0]
		v = strings.TrimSpace(v)
		if v == "" {
			return "", errors.New("checkpoint.load_path is empty")
		}
		unq, unqErr := strconv.Unquote(v)
		if unqErr == nil {
			v = unq
		}
		p := PathJoinRepo(repoRoot, v)
		if st, stErr := os.Stat(p); stErr != nil || st.IsDir() {
			return filepath.ToSlash(v), nil
		}
		return "", nil
	}
	return "", nil
}

// writeConfigOmittingCheckpointLoadPath copies src TOML to a temp file with any
// checkpoint.load_path line removed (used when the user confirms training
// without an existing resume file).
func writeConfigOmittingCheckpointLoadPath(srcAbs string) (tmpAbs string, err error) {
	raw, err := os.ReadFile(srcAbs)
	if err != nil {
		return "", err
	}
	lines := strings.Split(string(raw), "\n")
	inCheckpoint := false
	var out []string
	for _, ln := range lines {
		s := strings.TrimSpace(ln)
		if strings.HasPrefix(s, "[") && strings.HasSuffix(s, "]") {
			inCheckpoint = strings.EqualFold(s, "[checkpoint]")
			out = append(out, ln)
			continue
		}
		if inCheckpoint {
			keyPart := strings.TrimSpace(s)
			if idx := strings.IndexByte(keyPart, '='); idx >= 0 {
				keyPart = strings.TrimSpace(keyPart[:idx])
			}
			if strings.EqualFold(keyPart, "load_path") {
				continue
			}
		}
		out = append(out, ln)
	}
	f, err := os.CreateTemp("", "qmw-training-*.toml")
	if err != nil {
		return "", err
	}
	tmpAbs = f.Name()
	if _, werr := f.WriteString(strings.Join(out, "\n")); werr != nil {
		_ = f.Close()
		_ = os.Remove(tmpAbs)
		return "", werr
	}
	if cerr := f.Close(); cerr != nil {
		_ = os.Remove(tmpAbs)
		return "", cerr
	}
	return tmpAbs, nil
}

func PathJoinRepo(root, maybeRel string) string {
	p := filepath.Clean(maybeRel)
	if filepath.IsAbs(p) {
		return p
	}
	return filepath.Join(root, filepath.FromSlash(p))
}

// resolveRepoRelativePath joins user path to root and ensures the result stays inside root.
func resolveRepoRelativePath(root, user string) (abs string, err error) {
	user = strings.TrimSpace(user)
	if user == "" {
		return "", errors.New("path is required")
	}
	user = strings.TrimPrefix(user, "/")
	root = filepath.Clean(root)
	abs = filepath.Join(root, filepath.FromSlash(user))
	abs = filepath.Clean(abs)
	relRoot, e := filepath.Rel(root, abs)
	if e != nil || strings.HasPrefix(relRoot, "..") {
		return "", errors.New("path escapes repository root")
	}
	return abs, nil
}

type runStartOpts struct {
	RunTarget                  string `json:"run_target"`
	RunpodDestroyOnExit        bool   `json:"runpod_destroy_on_exit"`
	RunpodVarFile              string `json:"runpod_var_file"` // optional basename under infra/runpod, e.g. terraform.tfvars
	RunpodSkipApply            bool   `json:"runpod_skip_apply"`
	RunpodTrainOnPod           bool   `json:"runpod_train_on_pod"`           // ssh sync + engine on pod (default true for runpod)
	RunpodServerlessEndpointID string `json:"runpod_serverless_endpoint_id"` // optional override; else RUNPOD_SERVERLESS_ENDPOINT_ID
	RunpodWarmTargetID         string `json:"runpod_warm_target_id"`         // optional: use registered host; skip OpenTofu apply
}

func normalizeRunTarget(s string) string {
	s = strings.ToLower(strings.TrimSpace(s))
	if s == "" {
		return "local"
	}
	return s
}

func runStartOptsFromRequest(
	runTarget string,
	destroyPtr *bool,
	runpodVarFile string,
	skipApplyPtr, trainOnPodPtr *bool,
	runpodServerlessEndpointID string,
	runpodWarmTargetID string,
) runStartOpts {
	rt := normalizeRunTarget(runTarget)
	destroy := true
	if destroyPtr != nil {
		destroy = *destroyPtr
	}
	if rt != "runpod" {
		destroy = false
	}
	if rt == "runpod_serverless" {
		destroy = false
	}
	o := runStartOpts{
		RunTarget:                  rt,
		RunpodDestroyOnExit:        destroy,
		RunpodVarFile:              strings.TrimSpace(runpodVarFile),
		RunpodSkipApply:            false,
		RunpodTrainOnPod:           rt == "runpod",
		RunpodServerlessEndpointID: strings.TrimSpace(runpodServerlessEndpointID),
		RunpodWarmTargetID:         strings.TrimSpace(runpodWarmTargetID),
	}
	if rt == "runpod" {
		if skipApplyPtr != nil {
			o.RunpodSkipApply = *skipApplyPtr
		}
		if trainOnPodPtr != nil {
			o.RunpodTrainOnPod = *trainOnPodPtr
		}
	} else {
		o.RunpodTrainOnPod = false
		o.RunpodSkipApply = false
	}
	if rt != "runpod_serverless" {
		o.RunpodServerlessEndpointID = ""
	}
	return o
}

func validateRunTarget(o runStartOpts) error {
	switch o.RunTarget {
	case "local", "runpod", "runpod_serverless":
		return nil
	default:
		return errors.New("run_target must be local, runpod, or runpod_serverless")
	}
}

// hfExtraSpec is an additional Hugging Face dataset (primary uses data_path + hf_dataset_config).
type hfExtraSpec struct {
	Path          string `json:"path"`
	DatasetConfig string `json:"dataset_config"`
}

type buildRunRequest struct {
	Name                string        `json:"name"`
	Accelerator         string        `json:"accelerator"`
	QuantumBackend      string        `json:"quantum_backend"`
	QuantumPolicy       string        `json:"quantum_policy"`
	IBMBackendName      string        `json:"ibm_backend_name"`
	DataSource          string        `json:"data_source"`
	DataPath            string        `json:"data_path"`
	HFDatasetConfig     string        `json:"hf_dataset_config"`
	HFSplit             string        `json:"hf_split"`
	HFMeshBlend         float64       `json:"hf_mesh_blend_fraction"`
	HFNumSamples        int           `json:"hf_num_samples"`
	HFExtraSpecs        []hfExtraSpec `json:"hf_extra_specs"`
	Epochs              int           `json:"epochs"`
	BatchSize           int           `json:"batch_size"`
	LearningRate        float64       `json:"learning_rate"`
	// Optional training hyperparameters for generated TOML (nil = omit key).
	DataloaderNumWorkers *int     `json:"dataloader_num_workers,omitempty"`
	LrPlateauPatience    *int     `json:"lr_plateau_patience,omitempty"`
	LrPlateauFactor      *float64 `json:"lr_plateau_factor,omitempty"`
	GradClipNorm         *float64 `json:"grad_clip_norm,omitempty"`
	AutoStabilize        *bool    `json:"auto_stabilize,omitempty"`
	LRTrialMode         bool          `json:"lr_trial_mode"`
	LRTrialEpochs       int           `json:"lr_trial_epochs"`
	ResumeLatest        bool          `json:"resume_latest"`
	RunTarget           string        `json:"run_target"`
	RunpodDestroyOnExit *bool         `json:"runpod_destroy_on_exit"`
	RunpodVarFile       string        `json:"runpod_var_file"`
	// Optional [model] / [distributed] for generated training TOML (0 / false = omit).
	ModelDModel                 int  `json:"d_model,omitempty"`
	ModelNumTernaryBlocks       int  `json:"num_ternary_blocks,omitempty"`
	ModelIoDModel               int  `json:"io_d_model,omitempty"`
	ModelTropicalAttnPerBlock   bool `json:"tropical_attn_per_block,omitempty"`
	DistGradientCheckpointing   bool `json:"gradient_checkpointing,omitempty"`
	DistGradientAccumulation    int  `json:"gradient_accumulation_steps,omitempty"`
	DistAmp                     bool `json:"amp,omitempty"`
	DistFsdp                    bool `json:"fsdp,omitempty"`
	DistDdp                     bool `json:"ddp,omitempty"`
}

func handleRunBuild(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var body buildRunRequest
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonErr(w, http.StatusBadRequest, "invalid JSON")
		return
	}
	if normalizeQuantumPolicy(body.QuantumPolicy) == "" {
		jsonErr(
			w,
			http.StatusBadRequest,
			"quantum_policy must be hardware_only, prefer_hardware_fallback, or simulator_only",
		)
		return
	}
	accel := strings.ToLower(strings.TrimSpace(body.Accelerator))
	if accel == "" {
		accel = "cpu"
	}
	switch accel {
	case "cpu", "cuda", "xpu", "sycl":
	default:
		jsonErr(w, http.StatusBadRequest, "accelerator must be cpu/cuda/xpu/sycl")
		return
	}
	srcRaw := strings.ToLower(strings.TrimSpace(body.DataSource))
	if srcRaw == "" {
		srcRaw = "mesh"
	}
	if srcRaw != "mesh" && srcRaw != "hf_tabular" && srcRaw != "hybrid_mesh_hf_tabular" {
		jsonErr(w, http.StatusBadRequest, "data_source must be mesh, hf_tabular, or hybrid_mesh_hf_tabular")
		return
	}
	dp := strings.TrimSpace(body.DataPath)
	if (srcRaw == "hf_tabular" || srcRaw == "hybrid_mesh_hf_tabular") && dp == "" {
		jsonErr(w, http.StatusBadRequest, "data_path is required for hf_tabular")
		return
	}
	nExtras := countNonEmptyHFExtraSpecs(body.HFExtraSpecs)
	if isHFMultiPrimaryPlaceholder(dp) {
		if nExtras == 0 {
			jsonErr(
				w,
				http.StatusBadRequest,
				"hf_extra_specs must list at least one Hub dataset when data_path is qminiwasm/hf-multi (extras-only mode)",
			)
			return
		}
	}
	if body.Epochs <= 0 {
		body.Epochs = 25
	}
	if body.BatchSize <= 0 {
		body.BatchSize = 32
	}
	if body.LearningRate <= 0 {
		body.LearningRate = 1.0e-4
	}
	if body.LRTrialMode {
		if body.LRTrialEpochs <= 0 {
			body.LRTrialEpochs = 2
		}
		body.Epochs = body.LRTrialEpochs
	}
	if body.HFSplit == "" {
		body.HFSplit = "train"
	}
	if body.HFMeshBlend < 0 {
		body.HFMeshBlend = 0
	}
	src := srcRaw
	if srcRaw == "hybrid_mesh_hf_tabular" {
		// Hybrid mode uses hf_tabular source with required positive mesh blend.
		src = "hf_tabular"
		if body.HFMeshBlend <= 0 {
			body.HFMeshBlend = 0.20
		}
	}

	genDir := filepath.Join(repoRoot, "configs", "training")
	if err := os.MkdirAll(genDir, 0o755); err != nil {
		jsonErr(w, http.StatusInternalServerError, "failed to prepare config directory")
		return
	}
	name := sanitizeConfigName(body.Name)
	if name == "" {
		name = "model_" + strconv.FormatInt(time.Now().Unix(), 10)
	}
	rel := wuiWorkingConfigRel
	abs := filepath.Join(repoRoot, filepath.FromSlash(rel))
	_, _, latestRel := checkpointPathsInModelDir(name)
	latestAbs := filepath.Join(repoRoot, filepath.FromSlash(latestRel))
	if body.ResumeLatest {
		if st, err := os.Stat(latestAbs); err != nil || st.IsDir() {
			jsonErr(
				w,
				http.StatusBadRequest,
				"resume requested but latest checkpoint is missing: "+latestRel,
			)
			return
		}
	}

	content := buildGeneratedTOML(body, accel, src, srcRaw, name)
	if err := os.WriteFile(abs, []byte(content), 0o644); err != nil {
		jsonErr(w, http.StatusInternalServerError, "failed to write generated config")
		return
	}
	if err := writeAgentDeploymentFiles(repoRoot, name, rel); err != nil {
		log.Printf("training-wui: agent deployment sidecar files: %v", err)
	}
	finalRel, bestRel, _ := checkpointPathsInModelDir(name)
	bundleRel := filepath.ToSlash(filepath.Join(artifactModelDir(name), "agent_bundle.json"))
	serveRel := filepath.ToSlash(filepath.Join(artifactModelDir(name), "serve.toml"))
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	_ = json.NewEncoder(w).Encode(map[string]any{
		"config":  rel,
		"message": "saved working training config (use Launch training to start)",
		"agent": map[string]any{
			"model_dir":        artifactModelDir(name),
			"checkpoint_best":  bestRel,
			"checkpoint_final": finalRel,
			"serve_toml":       serveRel,
			"agent_bundle":     bundleRel,
		},
	})
}

func handleModelFacts(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var body buildRunRequest
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonErr(w, http.StatusBadRequest, "invalid JSON")
		return
	}
	accel := strings.ToLower(strings.TrimSpace(body.Accelerator))
	if accel == "" {
		accel = "cpu"
	}
	srcRaw := strings.ToLower(strings.TrimSpace(body.DataSource))
	if srcRaw == "" {
		srcRaw = "mesh"
	}
	src := srcRaw
	if srcRaw == "hybrid_mesh_hf_tabular" {
		src = "hf_tabular"
		if body.HFMeshBlend <= 0 {
			body.HFMeshBlend = 0.20
		}
	}
	if body.Epochs <= 0 {
		body.Epochs = 25
	}
	if body.BatchSize <= 0 {
		body.BatchSize = 32
	}
	if body.LearningRate <= 0 {
		body.LearningRate = 1.0e-4
	}
	if body.LRTrialMode {
		if body.LRTrialEpochs <= 0 {
			body.LRTrialEpochs = 2
		}
		body.Epochs = body.LRTrialEpochs
	}
	if body.HFSplit == "" {
		body.HFSplit = "train"
	}
	modelStem := sanitizeConfigName(body.Name)
	if modelStem == "" {
		modelStem = "model_" + strconv.FormatInt(time.Now().Unix(), 10)
	}
	tmpCfg, err := os.CreateTemp("", "qmw_model_facts_*.toml")
	if err != nil {
		jsonErr(w, http.StatusInternalServerError, "failed to create temp config")
		return
	}
	defer os.Remove(tmpCfg.Name())
	content := buildGeneratedTOML(body, accel, src, srcRaw, modelStem)
	if _, err := tmpCfg.WriteString(content); err != nil {
		_ = tmpCfg.Close()
		jsonErr(w, http.StatusInternalServerError, "failed to write temp config")
		return
	}
	_ = tmpCfg.Close()

	py := `
import json
import os
from dataclasses import replace
from pathlib import Path
from qminiwasm.engine.config import EngineConfig
from qminiwasm.model import QMiniWASM

cfg_path = os.environ["QMW_FACTS_CONFIG"]
model_stem = os.environ["QMW_FACTS_STEM"]
repo_root = Path(os.environ["QMW_FACTS_REPO"]).resolve()
cfg = EngineConfig.from_training_toml(cfg_path)
wasm_rt = replace(cfg.wasm_runtime_kwargs()["runtime"], force_mock=True)
m = QMiniWASM(
    device=None,
    use_hybrid_adapter=bool(getattr(cfg, "hybrid_adapter", False)),
    hybrid_adapter_hidden=int(getattr(cfg, "hybrid_adapter_hidden", 1024)),
    tequila_deadzone=float(getattr(cfg, "tequila_deadzone", 0.0) or 0.0),
    lota_rank=int(getattr(cfg, "lota_rank", 0) or 0),
    use_cascade_router=bool(getattr(cfg, "use_cascade_router", False)),
    cascade_state_dim=int(getattr(cfg, "cascade_state_dim", 8) or 8),
    cascade_num_actions=int(getattr(cfg, "cascade_num_actions", 4) or 4),
    cascade_router_hidden=int(getattr(cfg, "cascade_router_hidden", 32) or 32),
    wasm_runtime=wasm_rt,
    d_model=int(getattr(cfg, "d_model", 4096) or 4096),
    num_ternary_blocks=int(getattr(cfg, "num_ternary_blocks", 1) or 1),
    io_d_model=int(getattr(cfg, "io_d_model", 4096) or 4096),
    tropical_attn_per_block=bool(getattr(cfg, "tropical_attn_per_block", False)),
    use_gradient_checkpointing=bool(getattr(cfg, "gradient_checkpointing", False)),
)

def count_params(obj):
    if obj is None:
        return 0
    return int(sum(p.numel() for p in obj.parameters()))

parts = {
    "quantum_router": count_params(m.quantum_router),
    "ternary_stack": sum(count_params(b) for b in m.ternary_blocks),
    "io_stem": count_params(m.input_stem),
    "io_head": count_params(m.output_head) if m.output_head is not None else 0,
    "lota_branch": count_params(m.lota_branch),
    "hybrid_adapter": count_params(m.hybrid_adapter),
    "cascade_router": count_params(m.cascade_router),
    "tropical_attention": count_params(m.tropical_attention),
}
total = int(sum(parts.values()))
trainable_backbone = int(sum(p.numel() for p in m.trainable_hybrid_backbone_parameters()))
fp32_gb = total * 4 / (1024**3)
fp16_gb = total * 2 / (1024**3)
int8_gb = total * 1 / (1024**3)

save_path = getattr(cfg, "checkpoint_save_path", None) or ""
best_path = getattr(cfg, "checkpoint_best_path", None) or ""
latest_path = getattr(cfg, "checkpoint_latest_path", None) or ""
latest_abs = (repo_root / latest_path) if latest_path else None
agent_dir = f"artifacts/models/{model_stem}"
out = {
  "model_name": model_stem,
  "architecture": "QMiniWASM",
  "requested_accelerator": cfg.accelerator or "auto",
  "parameter_count_total": total,
  "parameter_count_trainable_backbone": trainable_backbone,
  "parameter_breakdown": parts,
  "estimated_size_gb": {"fp32": fp32_gb, "fp16": fp16_gb, "int8": int8_gb},
  "dataset": {
    "source": cfg.training_data_source,
    "id_path": cfg.data_path or "",
    "hf_dataset_config": cfg.hf_dataset_config or "",
    "hf_split": cfg.hf_split or "",
    "hf_mesh_blend_fraction": float(getattr(cfg, "hf_mesh_blend_fraction", 0.0) or 0.0),
    "hf_num_samples": getattr(cfg, "hf_num_samples", None),
    "hf_extra_specs": list(getattr(cfg, "hf_extra_specs", None) or []),
    "hf_token_present": bool(getattr(cfg, "hf_token", None)),
  },
  "artifacts": {
    "save_path": save_path,
    "best_path": best_path,
    "latest_path": latest_path,
    "serve_config": f"{agent_dir}/serve.toml",
    "agent_bundle": f"{agent_dir}/agent_bundle.json",
    "resume_available": bool(latest_abs and latest_abs.is_file()),
    "resume_requested": bool(int(os.environ.get("QMW_FACTS_RESUME_REQUESTED", "0"))),
  },
}
print(json.dumps(out))
`
	ctx, cancel := context.WithTimeout(r.Context(), 60*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, pythonExe, "-c", py)
	cmd.Dir = repoRoot
	env := os.Environ()
	env = append(env, "QMW_FACTS_CONFIG="+tmpCfg.Name())
	env = append(env, "QMW_FACTS_STEM="+modelStem)
	env = append(env, "QMW_FACTS_REPO="+repoRoot)
	if body.ResumeLatest {
		env = append(env, "QMW_FACTS_RESUME_REQUESTED=1")
	} else {
		env = append(env, "QMW_FACTS_RESUME_REQUESTED=0")
	}
	env = append(env, "LOG_LEVEL=ERROR")
	cmd.Env = env
	var outb bytes.Buffer
	var errb bytes.Buffer
	cmd.Stdout = &outb
	cmd.Stderr = &errb
	if err := cmd.Run(); err != nil {
		msg := strings.TrimSpace(errb.String())
		if msg == "" {
			msg = err.Error()
		}
		jsonErr(w, http.StatusInternalServerError, "model facts failed: "+msg)
		return
	}
	var parsed map[string]any
	if err := json.Unmarshal(bytes.TrimSpace(outb.Bytes()), &parsed); err != nil {
		jsonErr(w, http.StatusInternalServerError, "model facts failed: invalid JSON")
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok": true,
		"facts": parsed,
		// Python QMiniWASM facts; native gRPC engine does not load HF/tabular dataset_uri yet.
		"native_dataset_parity": false,
		"native_dataset_note": "The native gRPC C++ training engine uses a fixed synthetic balanced sampler; Hugging Face / tabular paths in TOML are not consumed by the C++ path yet. Parameter counts and sizes below are from the Python reference model.",
	})
}

func handleAutoLR(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var body buildRunRequest
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonErr(w, http.StatusBadRequest, "invalid JSON")
		return
	}
	accel := strings.ToLower(strings.TrimSpace(body.Accelerator))
	if accel == "" {
		accel = "cpu"
	}
	srcRaw := strings.ToLower(strings.TrimSpace(body.DataSource))
	if srcRaw == "" {
		srcRaw = "mesh"
	}
	src := srcRaw
	if srcRaw == "hybrid_mesh_hf_tabular" {
		src = "hf_tabular"
		if body.HFMeshBlend <= 0 {
			body.HFMeshBlend = 0.20
		}
	}
	dpLR := strings.TrimSpace(body.DataPath)
	if src == "hf_tabular" && dpLR == "" {
		jsonErr(w, http.StatusBadRequest, "data_path is required for hf_tabular/hybrid")
		return
	}
	if src == "hf_tabular" && isHFMultiPrimaryPlaceholder(dpLR) && countNonEmptyHFExtraSpecs(body.HFExtraSpecs) == 0 {
		jsonErr(
			w,
			http.StatusBadRequest,
			"hf_extra_specs required when data_path is qminiwasm/hf-multi (extras-only mode)",
		)
		return
	}
	if body.Epochs <= 0 {
		body.Epochs = 25
	}
	if body.BatchSize <= 0 {
		body.BatchSize = 32
	}
	if body.LearningRate <= 0 {
		body.LearningRate = 1.0e-4
	}
	if body.HFSplit == "" {
		body.HFSplit = "train"
	}
	tmpCfg, err := os.CreateTemp("", "qmw_lr_base_*.toml")
	if err != nil {
		jsonErr(w, http.StatusInternalServerError, "failed to create temporary config")
		return
	}
	defer os.Remove(tmpCfg.Name())
	content := buildGeneratedTOML(body, accel, src, srcRaw, "lr_trial_temp")
	if _, err := tmpCfg.WriteString(content); err != nil {
		_ = tmpCfg.Close()
		jsonErr(w, http.StatusInternalServerError, "failed to write temporary config")
		return
	}
	_ = tmpCfg.Close()

	script := filepath.Join(repoRoot, "scripts", "tune_learning_rate.py")
	ctx, cancel := context.WithTimeout(r.Context(), 8*time.Minute)
	defer cancel()
	cmd := exec.CommandContext(
		ctx,
		pythonExe,
		script,
		"--base-config",
		tmpCfg.Name(),
		"--candidate-lrs",
		"3e-5,1e-4,3e-4",
		"--trial-epochs",
		"2",
		"--max-runtime-seconds",
		"120",
		"--json",
	)
	cmd.Dir = repoRoot
	cmd.Env = os.Environ()
	var outb bytes.Buffer
	var errb bytes.Buffer
	cmd.Stdout = &outb
	cmd.Stderr = &errb
	if err := cmd.Run(); err != nil {
		msg := strings.TrimSpace(errb.String())
		if msg == "" {
			msg = err.Error()
		}
		jsonErr(w, http.StatusInternalServerError, "auto LR failed: "+msg)
		return
	}
	var parsed map[string]any
	if err := json.Unmarshal(bytes.TrimSpace(outb.Bytes()), &parsed); err != nil {
		jsonErr(w, http.StatusInternalServerError, "auto LR failed: invalid tuner output")
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok":                 true,
		"recommended_lr":     parsed["recommended_lr"],
		"promoted":           parsed["promoted"],
		"reason":             parsed["promotion_reason"],
		"metadata_path":      parsed["metadata_path"],
		"recommended_config": parsed["recommended_config"],
	})
}

func sanitizeConfigName(s string) string {
	s = strings.TrimSpace(strings.ToLower(s))
	if s == "" {
		return ""
	}
	var b strings.Builder
	lastDash := false
	for _, r := range s {
		ok := (r >= 'a' && r <= 'z') || (r >= '0' && r <= '9')
		if ok {
			b.WriteRune(r)
			lastDash = false
			continue
		}
		if !lastDash {
			b.WriteByte('-')
			lastDash = true
		}
	}
	out := strings.Trim(b.String(), "-")
	if out == "" {
		return ""
	}
	return out
}

func normalizeQuantumPolicy(policy string) string {
	switch strings.ToLower(strings.TrimSpace(policy)) {
	case "", "prefer_hardware_fallback":
		return "prefer_hardware_fallback"
	case "hardware_only":
		return "hardware_only"
	case "simulator_only":
		return "simulator_only"
	default:
		return ""
	}
}

func buildQuantumEnvOverrides(quantumBackend, quantumPolicy, ibmBackendName string) []string {
	var out []string
	qp := normalizeQuantumPolicy(quantumPolicy)
	qb := effectiveQuantumBackendForPolicy(quantumBackend, qp, ibmBackendName)
	if qb != "" {
		out = append(out, "QUANTUM_BACKEND="+qb)
	}
	if qp != "" {
		out = append(out, "QUANTUM_EXECUTION_POLICY="+qp)
	}
	if ibm := strings.TrimSpace(ibmBackendName); ibm != "" {
		out = append(out, "IBM_BACKEND_NAME="+ibm)
	}
	return out
}

func effectiveQuantumBackendForPolicy(quantumBackend, quantumPolicy, ibmBackendName string) string {
	qb := strings.ToLower(strings.TrimSpace(quantumBackend))
	qp := normalizeQuantumPolicy(quantumPolicy)
	ibm := strings.TrimSpace(ibmBackendName)

	switch qp {
	case "simulator_only":
		// Keep explicit simulator backend if provided, otherwise use local default.
		if qb != "" && qb != "auto" {
			return qb
		}
		return "penny_lane"
	case "hardware_only", "prefer_hardware_fallback":
		// In hardware modes, do not silently keep penny_lane.
		if ibm != "" {
			return ibm
		}
		if qb != "" && qb != "penny_lane" {
			return qb
		}
		return "auto"
	default:
		if qb != "" {
			return qb
		}
		return ""
	}
}

func buildGeneratedTOML(body buildRunRequest, accel, src, srcRaw, modelStem string) string {
	var b strings.Builder
	b.WriteString("# Generated by training-wui (Build wizard / working config)\n")
	if body.LRTrialMode {
		b.WriteString("# Mode: learning-rate trial run (short budget)\n")
	}
	if srcRaw == "hybrid_mesh_hf_tabular" {
		b.WriteString("# Requested data_source=hybrid_mesh_hf_tabular -> source=hf_tabular + mesh_blend_fraction>0\n")
	}
	b.WriteString("\n")
	b.WriteString("[hardware]\n")
	b.WriteString("accelerator = " + strconv.Quote(accel) + "\n")
	if qbe := effectiveQuantumBackendForPolicy(body.QuantumBackend, body.QuantumPolicy, body.IBMBackendName); qbe != "" {
		b.WriteString("quantum_backend = " + strconv.Quote(qbe) + "\n")
	}
	b.WriteString("device_index = 0\n\n")
	b.WriteString("[training]\n")
	b.WriteString("epochs = " + strconv.Itoa(body.Epochs) + "\n")
	b.WriteString("batch_size = " + strconv.Itoa(body.BatchSize) + "\n")
	b.WriteString("learning_rate = " + strconv.FormatFloat(body.LearningRate, 'g', -1, 64) + "\n")
	gcn := 1.0
	if body.GradClipNorm != nil && *body.GradClipNorm > 0 {
		gcn = *body.GradClipNorm
	}
	b.WriteString("grad_clip_norm = " + strconv.FormatFloat(gcn, 'g', -1, 64) + "\n")
	if body.DataloaderNumWorkers != nil {
		b.WriteString("dataloader_num_workers = " + strconv.Itoa(*body.DataloaderNumWorkers) + "\n")
	}
	if body.LrPlateauPatience != nil {
		b.WriteString("lr_plateau_patience = " + strconv.Itoa(*body.LrPlateauPatience) + "\n")
	}
	if body.LrPlateauFactor != nil {
		b.WriteString("lr_plateau_factor = " + strconv.FormatFloat(*body.LrPlateauFactor, 'g', -1, 64) + "\n")
	}
	if body.AutoStabilize != nil && *body.AutoStabilize {
		b.WriteString("auto_stabilize = true\n")
	}
	b.WriteString("\n")
	finalRel, bestRel, latestRel := checkpointPathsInModelDir(modelStem)
	b.WriteString("[checkpoint]\n")
	b.WriteString("save_path = " + strconv.Quote(finalRel) + "\n")
	b.WriteString("best_path = " + strconv.Quote(bestRel) + "\n")
	b.WriteString("latest_path = " + strconv.Quote(latestRel) + "\n\n")
	if body.ResumeLatest {
		b.WriteString("load_path = " + strconv.Quote(latestRel) + "\n\n")
	}
	b.WriteString("[data]\n")
	b.WriteString("source = " + strconv.Quote(src) + "\n")
	if src == "hf_tabular" {
		var extras []hfExtraSpec
		for _, ex := range body.HFExtraSpecs {
			if p := strings.TrimSpace(ex.Path); p != "" {
				extras = append(extras, ex)
			}
		}
		rawDP := strings.TrimSpace(body.DataPath)
		dataPathOut := rawDP
		dcOut := strings.TrimSpace(body.HFDatasetConfig)
		// Never emit hf-multi without extra_specs (invalid for engine / Model Facts temp TOMLs).
		if isHFMultiPrimaryPlaceholder(rawDP) && len(extras) == 0 {
			b.WriteString("# data.path was qminiwasm/hf-multi with no hf_extra_specs; using default primary Hub dataset so the TOML is valid.\n")
			dataPathOut = "code-search-net/code_search_net"
			if dcOut == "" {
				dcOut = "python"
			}
		} else if isHFMultiPrimaryPlaceholder(rawDP) {
			b.WriteString("# data.path is a reserved placeholder; training loads only [huggingface].extra_specs.\n")
		}
		b.WriteString("path = " + strconv.Quote(dataPathOut) + "\n")
	}
	b.WriteString("mesh_algorithms = \"hash,encrypt,network,routing,consensus\"\n\n")
	if src == "hf_tabular" {
		var extras []hfExtraSpec
		for _, ex := range body.HFExtraSpecs {
			if p := strings.TrimSpace(ex.Path); p != "" {
				extras = append(extras, ex)
			}
		}
		rawDP := strings.TrimSpace(body.DataPath)
		dcOut := strings.TrimSpace(body.HFDatasetConfig)
		if isHFMultiPrimaryPlaceholder(rawDP) && len(extras) == 0 && dcOut == "" {
			dcOut = "python"
		}
		b.WriteString("[huggingface]\n")
		if dcOut != "" {
			b.WriteString("dataset_config = " + strconv.Quote(dcOut) + "\n")
		}
		b.WriteString("split = " + strconv.Quote(strings.TrimSpace(body.HFSplit)) + "\n")
		if body.HFNumSamples > 0 {
			b.WriteString("num_samples = " + strconv.Itoa(body.HFNumSamples) + "\n")
		}
		b.WriteString(
			"mesh_blend_fraction = " + strconv.FormatFloat(body.HFMeshBlend, 'g', -1, 64) + "\n",
		)
		if len(extras) > 0 {
			b.WriteString("extra_specs = [\n")
			for _, ex := range extras {
				p := strings.TrimSpace(ex.Path)
				b.WriteString("  { path = " + strconv.Quote(p))
				if c := strings.TrimSpace(ex.DatasetConfig); c != "" {
					b.WriteString(", dataset_config = " + strconv.Quote(c))
				}
				b.WriteString(" },\n")
			}
			b.WriteString("]\n")
		}
		b.WriteString("\n")
	}
	if body.LRTrialMode {
		b.WriteString("[eval]\n")
		b.WriteString("holdout_fraction = 0.05\n")
		b.WriteString("every_epoch = true\n")
		b.WriteString("early_stop_patience = 3\n\n")
	}
	b.WriteString("[cascade]\n")
	b.WriteString("enabled = true\n")
	b.WriteString("steps_per_epoch = 2\n")
	b.WriteString("group_size = 4\n")
	if body.ModelDModel > 0 || body.ModelNumTernaryBlocks > 0 || body.ModelIoDModel > 0 ||
		body.ModelTropicalAttnPerBlock {
		b.WriteString("\n[model]\n")
		if body.ModelDModel > 0 {
			b.WriteString("d_model = " + strconv.Itoa(body.ModelDModel) + "\n")
		}
		if body.ModelNumTernaryBlocks > 0 {
			b.WriteString("num_ternary_blocks = " + strconv.Itoa(body.ModelNumTernaryBlocks) + "\n")
		}
		if body.ModelIoDModel > 0 {
			b.WriteString("io_d_model = " + strconv.Itoa(body.ModelIoDModel) + "\n")
		}
		if body.ModelTropicalAttnPerBlock {
			b.WriteString("tropical_attn_per_block = true\n")
		}
	}
	if body.DistGradientCheckpointing || body.DistGradientAccumulation > 0 || body.DistAmp ||
		body.DistFsdp || body.DistDdp {
		b.WriteString("\n[distributed]\n")
		if body.DistGradientCheckpointing {
			b.WriteString("gradient_checkpointing = true\n")
		}
		if body.DistGradientAccumulation > 0 {
			b.WriteString("gradient_accumulation_steps = " + strconv.Itoa(body.DistGradientAccumulation) + "\n")
		}
		if body.DistAmp {
			b.WriteString("amp = true\n")
		}
		if body.DistFsdp {
			b.WriteString("fsdp = true\n")
		}
		if body.DistDdp {
			b.WriteString("ddp = true\n")
		}
	}
	return b.String()
}

// buildNativeGRPCTrainingConfigPreview maps the selected training TOML to the same TrainingConfig
// fields StartTraining sends (proto d_model / io_d_model / num_ternary_blocks / model_uri).
func buildNativeGRPCTrainingConfigPreview(absConfig string) (map[string]any, string) {
	if strings.TrimSpace(absConfig) == "" {
		return nil, ""
	}
	cfg, err := TrainingTOMLToProto(absConfig, "preflight", repoRoot)
	if err != nil {
		return nil, err.Error()
	}
	d := cfg.GetDModel()
	io := cfg.GetIoDModel()
	nb := cfg.GetNumTernaryBlocks()
	modelURISet := strings.TrimSpace(cfg.GetModelUri()) != ""

	out := map[string]any{
		"d_model":            d,
		"io_d_model":         io,
		"num_ternary_blocks": nb,
		"model_uri_set":      modelURISet,
	}
	if modelURISet {
		out["native_cold_start_note"] = "Checkpoint/tpem load path set: native engine loads weights/geometry from interchange; proto d/io/N are secondary."
	} else {
		var effD, effIO, effNB uint32
		if d > 0 {
			effD = d
			if io > 0 {
				effIO = io
			} else {
				effIO = d
			}
			if nb > 0 {
				effNB = nb
			} else {
				effNB = 1
			}
		} else {
			effD, effIO, effNB = 4096, 4096, 1
		}
		out["native_effective_d_model"] = effD
		out["native_effective_io_d_model"] = effIO
		out["native_effective_num_ternary_blocks"] = effNB
	}
	out["memory_estimate"] = computeNativeColdStartMemoryEstimateMap(cfg)
	if total, avail, ok := hostMemoryForPreflight(); ok {
		out["host_physical_memory_bytes"] = total
		out["host_available_physical_bytes"] = avail
		out["host_physical_memory_gib"] = float64(total) / (1024 * 1024 * 1024)
		if avail > 0 {
			out["host_available_physical_gib"] = float64(avail) / (1024 * 1024 * 1024)
		}
	}
	return out, ""
}

func handlePreflight(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	configRel := strings.TrimSpace(r.URL.Query().Get("config"))
	quantumBackend := strings.TrimSpace(r.URL.Query().Get("quantum_backend"))
	quantumPolicy := strings.TrimSpace(r.URL.Query().Get("quantum_policy"))
	ibmBackendName := strings.TrimSpace(r.URL.Query().Get("ibm_backend_name"))
	preflightAccel := strings.TrimSpace(r.URL.Query().Get("accelerator"))
	configAbs := ""
	if configRel != "" {
		abs, err := resolveTrainingConfig(repoRoot, configRel)
		if err != nil {
			jsonErr(w, http.StatusBadRequest, err.Error())
			return
		}
		configAbs = abs
	}
	probe := `
import json
import os
import sys

from qminiwasm.engine.config import EngineConfig
from qminiwasm.engine._dotenv import load_dotenv_if_available
from qminiwasm.hardware.device import get_device, get_xpu_backend_status, resolve_backend_policy
from qminiwasm.hardware.sycl_hardware import SYCLHardware

load_dotenv_if_available()

cfg_path = sys.argv[1] if len(sys.argv) > 1 else ""
if cfg_path:
    cfg = EngineConfig.from_training_toml(cfg_path)
else:
    cfg = EngineConfig()

preflight_accel = os.environ.get("QMW_PREFLIGHT_ACCELERATOR", "").strip()
accel_from_config = (cfg.accelerator or "").strip() or (os.environ.get("ACCELERATOR", "").strip() or "auto")
if preflight_accel:
    accel_for_device = preflight_accel
else:
    accel_for_device = accel_from_config
requested = accel_for_device
device = get_device(accelerator=accel_for_device, device_index=cfg.device_index)
policy = resolve_backend_policy(accelerator=accel_for_device, device_index=cfg.device_index)
sy = SYCLHardware()
is_active = False
if hasattr(sy, "is_backend_active"):
    try:
        is_active = bool(sy.is_backend_active())
    except Exception:
        is_active = False
else:
    is_active = getattr(sy, "device", None) is not None
sycl_status = {}
if hasattr(sy, "backend_status"):
    try:
        sycl_status = dict(sy.backend_status())
    except Exception:
        sycl_status = {}

dpctl_count = None
dpctl_error = ""
try:
    import dpctl
    dpctl_count = len(dpctl.get_devices())
except Exception as e:
    dpctl_error = str(e)

out = {
    "python_exe": sys.executable,
    "config": cfg_path,
    "requested_accelerator": requested,
    "accelerator_from_config_file": (cfg.accelerator or ""),
    "preflight_accelerator_override": preflight_accel,
    "resolved_torch_device": str(device),
    "backend_policy": policy,
    "sycl_backend_active": is_active,
    "sycl_backend_status": sycl_status,
    "dpctl_device_count": dpctl_count,
    "dpctl_error": dpctl_error,
    "quantum_backend": (os.environ.get("QUANTUM_BACKEND", "").strip() or cfg.quantum_backend or "penny_lane"),
    "quantum_backend_from_config": (cfg.quantum_backend or ""),
    "quantum_policy": (os.environ.get("QUANTUM_EXECUTION_POLICY", "").strip() or "prefer_hardware_fallback"),
    "default_runtime_profile": "optimized_auto",
    "training_runtime_mode": (os.environ.get("QMINIWASM_TRAINING_RUNTIME_MODE", "").strip() or "native"),
    "native_strict_enabled": os.environ.get("QMINIWASM_NATIVE_STRICT", "").strip().lower() in ("1", "true", "yes", "on"),
    "ternary_impl": (os.environ.get("QMINIWASM_TERNARY_IMPL", "").strip() or "auto"),
    "trit_pack_impl": (os.environ.get("QMINIWASM_TRIT_PACK_IMPL", "").strip() or "auto"),
    "memory_encode_impl": (os.environ.get("QMINIWASM_MEMORY_ENCODE_IMPL", "").strip() or "auto"),
    "wasm_exec_impl": (os.environ.get("QMINIWASM_WASM_EXEC_IMPL", "").strip() or "auto"),
    "cascade_rl_impl": (os.environ.get("QMINIWASM_CASCADE_RL_IMPL", "").strip() or "auto"),
    "tpem_native_bundle": (os.environ.get("QMINIWASM_TPEM_NATIVE_BUNDLE", "").strip() or "0"),
    "model_d_model": int(cfg.d_model),
    "model_io_d_model": int(cfg.io_d_model),
    "model_num_ternary_blocks": int(cfg.num_ternary_blocks),
}

vi = sys.version_info
out["python_version"] = f"{vi.major}.{vi.minor}.{vi.micro}"
out["xpu_support_class"] = None
out["xpu_device_name"] = None
out["intel_xpu_stack_advisory"] = ""
out["xpu_memory_allocated_bytes"] = None
out["xpu_max_memory_allocated_bytes"] = None
if getattr(device, "type", None) == "xpu":
    import torch

    xst = get_xpu_backend_status(cfg.device_index)
    out["xpu_support_class"] = xst.get("support_class")
    out["xpu_device_name"] = xst.get("device_name")
    advisory = []
    if vi.major == 3 and vi.minor >= 13:
        advisory.append(
            "Python 3.13+ may be ahead of Intel's published PyTorch XPU install matrix; "
            "see https://intel.github.io/intel-extension-for-pytorch/xpu/latest/ if XPU misbehaves."
        )
    if str(xst.get("support_class") or "") == "experimental":
        advisory.append(
            "Integrated XPU is experimental: raise [training].batch_size gradually for more shared GPU memory; "
            "use [training].dataloader_num_workers in TOML / WUI to overlap host batch prep."
        )
    out["intel_xpu_stack_advisory"] = " ".join(advisory)
    try:
        idx = device.index if device.index is not None else 0
        xmod = torch.xpu
        if hasattr(xmod, "memory_allocated"):
            out["xpu_memory_allocated_bytes"] = int(xmod.memory_allocated(idx))
        if hasattr(xmod, "max_memory_allocated"):
            out["xpu_max_memory_allocated_bytes"] = int(xmod.max_memory_allocated(idx))
    except Exception:
        pass

ibm = {
    "runtime_available": False,
    "token_present": bool(
        os.environ.get("IBM_QUANTUM_API_TOKEN", "").strip()
        or os.environ.get("QISKIT_IBM_TOKEN", "").strip()
    ),
    "connected": False,
    "requested_backend": (
        os.environ.get("IBM_BACKEND_NAME", "").strip()
        or out["quantum_backend"]
        or "auto"
    ),
    "selected_backend": "",
    "backend_operational": None,
    "backend_pending_jobs": None,
    "backend_status_msg": "",
    "usage_seconds": None,
    "usage_limit_seconds": None,
    "job_time_left_seconds": None,
    "connect_error": "",
}
try:
    from qiskit_ibm_runtime import QiskitRuntimeService
    ibm["runtime_available"] = True
    token = os.environ.get("IBM_QUANTUM_API_TOKEN", "").strip() or os.environ.get("QISKIT_IBM_TOKEN", "").strip()
    svc = None
    if token:
        for ch in ("ibm_quantum", "ibm_cloud", "ibm_quantum_platform"):
            try:
                svc = QiskitRuntimeService(channel=ch, token=token)
                break
            except Exception:
                svc = None
    if svc is None:
        svc = QiskitRuntimeService()
    ibm["connected"] = True
    req = str(ibm["requested_backend"]).strip().lower()
    backend = None
    if req and req not in ("auto", "penny_lane"):
        try:
            backend = svc.backend(req)
        except Exception:
            backend = None
    if backend is None:
        try:
            backend = svc.least_busy(operational=True, simulator=False)
        except Exception:
            backend = None
    if backend is not None:
        ibm["selected_backend"] = getattr(backend, "name", "") or str(backend)
        try:
            st = backend.status()
            ibm["backend_operational"] = bool(getattr(st, "operational", False))
            ibm["backend_pending_jobs"] = int(getattr(st, "pending_jobs", 0))
            ibm["backend_status_msg"] = str(getattr(st, "status_msg", ""))
        except Exception:
            pass
    try:
        usage_fn = getattr(svc, "usage", None)
        if callable(usage_fn):
            u = usage_fn()
            if isinstance(u, dict):
                for k in ("seconds", "usage_seconds", "used_seconds", "consumed_seconds"):
                    if u.get(k) is not None:
                        ibm["usage_seconds"] = float(u.get(k))
                        break
                for k in ("limit_seconds", "quota_seconds"):
                    if u.get(k) is not None:
                        ibm["usage_limit_seconds"] = float(u.get(k))
                        break
                if u.get("remaining_seconds") is not None:
                    ibm["job_time_left_seconds"] = float(u.get("remaining_seconds"))
    except Exception:
        pass
except Exception as e:
    ibm["connect_error"] = str(e)

out["ibm"] = ibm
print(json.dumps(out))
`
	ctx, cancel := context.WithTimeout(r.Context(), 45*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, pythonExe, "-c", probe, configAbs)
	cmd.Dir = repoRoot
	env := os.Environ()
	if qbe := effectiveQuantumBackendForPolicy(quantumBackend, quantumPolicy, ibmBackendName); qbe != "" {
		env = append(env, "QUANTUM_BACKEND="+qbe)
	}
	if qp := normalizeQuantumPolicy(quantumPolicy); qp != "" {
		env = append(env, "QUANTUM_EXECUTION_POLICY="+qp)
	}
	if ibm := strings.TrimSpace(ibmBackendName); ibm != "" {
		env = append(env, "IBM_BACKEND_NAME="+ibm)
	}
	if preflightAccel != "" {
		env = append(env, "QMW_PREFLIGHT_ACCELERATOR="+preflightAccel)
	}
	cmd.Env = env
	var outb bytes.Buffer
	var errb bytes.Buffer
	cmd.Stdout = &outb
	cmd.Stderr = &errb
	if err := cmd.Run(); err != nil {
		msg := strings.TrimSpace(errb.String())
		if msg == "" {
			msg = err.Error()
		}
		jsonErr(w, http.StatusInternalServerError, "preflight failed: "+msg)
		return
	}
	raw := strings.TrimSpace(outb.String())
	if raw == "" {
		jsonErr(w, http.StatusInternalServerError, "preflight failed: empty output")
		return
	}
	var parsed map[string]any
	if err := json.Unmarshal([]byte(raw), &parsed); err != nil {
		jsonErr(w, http.StatusInternalServerError, "preflight failed: invalid JSON output")
		return
	}
	rp := wuiResolved.RuntimeProfile
	parsed["training_runtime_mode"] = trainingRuntimeModeRaw()
	parsed["native_strict_enabled"] = rp.NativeStrictEnabled
	parsed["ternary_impl"] = rp.TernaryImpl
	parsed["trit_pack_impl"] = rp.TritPackImpl
	parsed["memory_encode_impl"] = rp.MemoryEncodeImpl
	parsed["wasm_exec_impl"] = rp.WasmExecImpl
	parsed["cascade_rl_impl"] = rp.CascadeRLImpl
	parsed["tpem_native_bundle"] = rp.TPEMNativeBundle
	grpcOK := trainingGRPCReachable(600 * time.Millisecond)
	parsed["grpc_engine_reachable"] = grpcOK
	parsed["grpc_engine_addr"] = trainingGRPCAddressResolved()
	useG, pickReason := trainingEnginePickLocal()
	wuiTE := map[string]any{
		"mode":                    trainingRuntimeModeRaw(),
		"grpc_addr":               trainingGRPCAddressResolved(),
		"grpc_reachable":          grpcOK,
		"pick_for_local_use_grpc": useG,
		"pick_reason":             pickReason,
	}
	resp := map[string]any{
		"ok":                  true,
		"preflight":           parsed,
		"wui_training_engine": wuiTE,
	}
	if natPreview, natErr := buildNativeGRPCTrainingConfigPreview(configAbs); natErr != "" {
		resp["native_grpc_training_config_error"] = natErr
	} else if natPreview != nil {
		resp["native_grpc_training_config"] = natPreview
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(resp)
}

func handleRunsItem(w http.ResponseWriter, r *http.Request) {
	path := strings.TrimPrefix(r.URL.Path, "/api/runs/")
	parts := strings.Split(path, "/")
	if len(parts) == 0 || parts[0] == "" {
		http.NotFound(w, r)
		return
	}
	id := parts[0]
	if len(parts) == 2 && parts[1] == "log" && r.Method == http.MethodGet {
		handleRunLog(w, r, id)
		return
	}
	if len(parts) == 2 && parts[1] == "ws" && r.Method == http.MethodGet {
		handleRunWS(w, r, id)
		return
	}
	if len(parts) == 2 && parts[1] == "stop" && r.Method == http.MethodPost {
		handleRunStop(w, r, id)
		return
	}
	if len(parts) == 2 && parts[1] == "cooperative-stop" && r.Method == http.MethodPost {
		handleRunCooperativeStop(w, r, id)
		return
	}
	http.NotFound(w, r)
}

var wsUpgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

func handleRunWS(w http.ResponseWriter, r *http.Request, id string) {
	if !authOK(r) {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}
	conn, err := wsUpgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	sub := wsHub.subscribe(id, conn)
	defer wsHub.unsubscribe(id, sub)
	for {
		if _, _, err := conn.ReadMessage(); err != nil {
			return
		}
	}
}

func handleRunLog(w http.ResponseWriter, r *http.Request, id string) {
	offset := int64(0)
	if o := r.URL.Query().Get("offset"); o != "" {
		fmt.Sscanf(o, "%d", &offset)
	}
	chunk, next, running, err := runManager.logChunk(id, offset)
	if err != nil {
		jsonErr(w, http.StatusNotFound, err.Error())
		return
	}
	// #region agent log
	{
		ln, _ := json.Marshal(map[string]any{
			"sessionId":    "4b1a8d",
			"hypothesisId": "D",
			"location":     "main.go:handleRunLog",
			"message":      "log_chunk",
			"data": map[string]any{
				"run_id":      id,
				"offset":      offset,
				"chunk_bytes": len(chunk),
				"next":        next,
				"running":     running,
			},
			"timestamp": time.Now().UnixMilli(),
		})
		if f, err := os.OpenFile(agentDebugLogPath, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644); err == nil {
			_, _ = f.Write(append(ln, '\n'))
			_ = f.Close()
		}
	}
	// #endregion agent log
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"text":        chunk,
		"next_offset": next,
		"running":     running,
		"exit_code":   runManager.exitCode(id),
	})
}

func handleRunStop(w http.ResponseWriter, r *http.Request, id string) {
	force := false
	if r.Body != nil {
		defer r.Body.Close()
		var body struct {
			Force bool `json:"force"`
		}
		dec := json.NewDecoder(io.LimitReader(r.Body, 4096))
		if err := dec.Decode(&body); err == nil {
			force = body.Force
		}
	}
	err := runManager.stop(id, force)
	if err != nil {
		jsonErr(w, http.StatusBadRequest, err.Error())
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "force": force})
}

func handleRunCooperativeStop(w http.ResponseWriter, r *http.Request, id string) {
	if r.Body != nil {
		defer r.Body.Close()
		_, _ = io.Copy(io.Discard, io.LimitReader(r.Body, 4096))
	}
	err := runManager.cooperativeStop(id)
	if err != nil {
		jsonErr(w, http.StatusBadRequest, err.Error())
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "cooperative": true})
}

func jsonErr(w http.ResponseWriter, code int, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(map[string]string{"error": msg})
}

func resolveTrainingConfig(root, user string) (abs string, err error) {
	user = strings.TrimSpace(user)
	user = strings.TrimPrefix(user, "/")
	root = filepath.Clean(root)
	abs = filepath.Join(root, filepath.FromSlash(user))
	abs = filepath.Clean(abs)
	relRoot, e := filepath.Rel(root, abs)
	if e != nil || strings.HasPrefix(relRoot, "..") {
		return "", errors.New("invalid config path")
	}
	base := filepath.Join(root, "configs", "training")
	base = filepath.Clean(base)
	relBase, e := filepath.Rel(base, abs)
	if e != nil || strings.HasPrefix(relBase, "..") {
		return "", errors.New("config must be under configs/training")
	}
	if strings.ToLower(filepath.Ext(abs)) != ".toml" {
		return "", errors.New("config must be a .toml file")
	}
	if st, e := os.Stat(abs); e != nil || st.IsDir() {
		return "", errors.New("config file not found")
	}
	return abs, nil
}

// --- run manager ---

type runWSSub struct {
	conn *websocket.Conn
	send chan []byte
	done chan struct{}
}

type runWSHub struct {
	mu   sync.Mutex
	subs map[string]map[*runWSSub]struct{}
}

func newRunWSHub() *runWSHub {
	return &runWSHub{subs: make(map[string]map[*runWSSub]struct{})}
}

func (h *runWSHub) subscribe(runID string, conn *websocket.Conn) *runWSSub {
	// Large buffer: gRPC C++ emits multiple telemetry rows per step/epoch; a small buffer caused constant drops.
	sub := &runWSSub{
		conn: conn,
		send: make(chan []byte, 2048),
		done: make(chan struct{}),
	}
	h.mu.Lock()
	if h.subs[runID] == nil {
		h.subs[runID] = make(map[*runWSSub]struct{})
	}
	h.subs[runID][sub] = struct{}{}
	h.mu.Unlock()
	go func() {
		for {
			select {
			case <-sub.done:
				return
			case msg := <-sub.send:
				_ = conn.SetWriteDeadline(time.Now().Add(5 * time.Second))
				if err := conn.WriteMessage(websocket.TextMessage, msg); err != nil {
					return
				}
			}
		}
	}()
	return sub
}

func (h *runWSHub) unsubscribe(runID string, sub *runWSSub) {
	h.mu.Lock()
	if m := h.subs[runID]; m != nil {
		delete(m, sub)
		if len(m) == 0 {
			delete(h.subs, runID)
		}
	}
	h.mu.Unlock()
	close(sub.done)
	_ = sub.conn.Close()
}

// noteWSBackpressure aggregates drops and logs at most once per window to avoid flooding the training log.
func (m *manager) noteWSBackpressure(runID string, dropped int) {
	if dropped <= 0 {
		return
	}
	const window = 10 * time.Second
	m.mu.Lock()
	r := m.byID[runID]
	if r == nil {
		m.mu.Unlock()
		return
	}
	r.wsBackpressureAccum += dropped
	r.WSDroppedMessages += dropped
	shouldEmit := r.lastWSBackpressureLog.IsZero() || time.Since(r.lastWSBackpressureLog) >= window
	var n int
	if shouldEmit && r.wsBackpressureAccum > 0 {
		n = r.wsBackpressureAccum
		r.wsBackpressureAccum = 0
		r.lastWSBackpressureLog = time.Now()
	}
	m.mu.Unlock()
	if n == 0 {
		return
	}
	msg := fmt.Sprintf(
		"ws backpressure: dropped %d websocket message(s) in ~%ds (client slower than telemetry); server buffer is large — if this repeats, reduce Live telemetry traffic (filter) or browser load\n",
		n, int(window/time.Second))
	log.Printf("run %s %s", runID, strings.TrimSpace(msg))
	m.appendLog(runID, []byte(msg), "stderr")
}

func (h *runWSHub) broadcast(runID string, payload map[string]any) {
	b, err := json.Marshal(payload)
	if err != nil {
		return
	}
	h.mu.Lock()
	m := h.subs[runID]
	snapshot := make([]*runWSSub, 0, len(m))
	for sub := range m {
		snapshot = append(snapshot, sub)
	}
	h.mu.Unlock()
	dropped := 0
	for _, sub := range snapshot {
		select {
		case <-sub.done:
		case sub.send <- b:
		default:
			// Prefer newest telemetry: drop one buffered message and retry once.
			select {
			case <-sub.done:
			case <-sub.send:
			default:
			}
			select {
			case <-sub.done:
			case sub.send <- b:
			default:
				dropped++
			}
		}
	}
	if dropped > 0 {
		runManager.noteWSBackpressure(runID, dropped)
	}
}

type runRecord struct {
	ID                    string    `json:"id"`
	ConfigRel             string    `json:"config"`
	Started               time.Time `json:"started"`
	Running               bool      `json:"running"`
	ExitCode              int       `json:"exit_code,omitempty"`
	Error                 bool      `json:"error"`
	RunTarget             string    `json:"run_target,omitempty"`
	RunpodDestroyOnExit   bool      `json:"runpod_destroy_on_exit,omitempty"`
	RunpodVarFile         string    `json:"runpod_var_file,omitempty"`
	ServerlessJobID       string    `json:"serverless_job_id,omitempty"`
	ServerlessEndpointID  string    `json:"serverless_endpoint_id,omitempty"`
	configCleanups        []string  `json:"-"` // temp TOML paths to remove after the process exits
	serverlessTomlOverlay string    `json:"-"` // RunPod input.toml_overlay (optional)
	cmd                   *exec.Cmd
	logMu                 sync.Mutex
	logBuf                bytes.Buffer
	stdoutLineBuf         strings.Builder
	stderrLineBuf         strings.Builder
	LastEpoch             int
	TotalEpochs           int
	LastMeanLoss          float64
	LastMeanReturn        float64
	LastMeanMSE           float64
	procExited            chan struct{} // closed after cmd.Wait (nil for serverless)
	XPUFallbackSeen       bool
	WasmMockSeen          bool
	PrunedFromNodes       int
	PrunedToNodes         int
	QAOASimMS             float64
	QPUEstMS              float64
	WSDroppedMessages     int
	lastWSBackpressureLog time.Time // rate-limit backpressure lines in the run log
	wsBackpressureAccum   int
	BackendRequested      string
	BackendSelected       string
	BackendReasonCode     string
	XPUSupportClass       string
	SYCLActive            bool
	SYCLBackend           string
	SYCLDevice            string
	SYCLFallback          string
	SYCLDPCTLCount        int
	LastRoutingState      int
	LastAssignmentMS      float64
	RoutingBudgetMS       float64
	maxRuns               int                // ring of finished ids for list
	trainCancel           context.CancelFunc // native gRPC: cancels StartTraining/dial ctx only (not telemetry stream)
	nativeStreamCancel    context.CancelFunc // native gRPC: cancel StreamTelemetry recv (force stop)
	wuiStopFile           string             // local abs path, or remote abs path for cooperative stop
	runpodCoopHost        string             // SSH host when training on RunPod pod (cooperative stop via touch)
	runpodCoopUser        string
	runpodCoopKeyPath     string
}

type manager struct {
	mu      sync.Mutex
	byID    map[string]*runRecord
	ordered []string
}

func newManager() *manager {
	return &manager{byID: make(map[string]*runRecord)}
}

func newRunID() string {
	b := make([]byte, 8)
	_, _ = rand.Read(b)
	return hex.EncodeToString(b)
}

func (m *manager) start(absConfig, relDisplay string, extraEnv []string, opts runStartOpts, configCleanups []string, serverlessTomlOverlay string) (*runRecord, error) {
	opts.RunTarget = normalizeRunTarget(opts.RunTarget)
	if err := validateRunTarget(opts); err != nil {
		return nil, err
	}
	if serveProcessRunning() {
		return nil, fmt.Errorf("stop the serve process (POST /api/serve/stop) before starting training")
	}

	m.mu.Lock()
	for _, id := range m.ordered {
		if r := m.byID[id]; r != nil && r.Running {
			m.mu.Unlock()
			return nil, fmt.Errorf("a run is already in progress (%s)", id[:8])
		}
	}
	m.mu.Unlock()

	warmIP := ""
	warmSSHUser := ""
	if tid := strings.TrimSpace(opts.RunpodWarmTargetID); tid != "" {
		if opts.RunTarget != "runpod" || !opts.RunpodTrainOnPod {
			return nil, errors.New("runpod_warm_target_id requires run_target runpod with train on remote GPU (SSH)")
		}
		f, err := loadWarmTargets()
		if err != nil {
			return nil, fmt.Errorf("warm targets: %w", err)
		}
		t := findWarmTargetByID(f.Targets, tid)
		if t == nil {
			return nil, fmt.Errorf("warm target not found: %s", tid)
		}
		warmIP = strings.TrimSpace(t.Host)
		if warmIP == "" {
			return nil, fmt.Errorf("warm target %q has empty host", tid)
		}
		if u := strings.TrimSpace(t.SSHUser); u != "" {
			warmSSHUser = u
		}
		opts.RunpodSkipApply = true
	}

	var applyLog string
	applySucceeded := false
	if opts.RunTarget == "runpod" && !opts.RunpodSkipApply {
		ctx, cancel := context.WithTimeout(context.Background(), 25*time.Minute)
		defer cancel()
		var err error
		applyLog, err = runpodApply(ctx, opts.RunpodVarFile)
		if err != nil {
			return nil, fmt.Errorf("runpod OpenTofu apply failed: %v\n\n--- tofu output ---\n%s", err, applyLog)
		}
		applySucceeded = true
	}

	registered := false
	m.mu.Lock()
	defer func() {
		m.mu.Unlock()
		if !registered && applySucceeded && opts.RunTarget == "runpod" && opts.RunpodDestroyOnExit {
			ctx, cancel := context.WithTimeout(context.Background(), 20*time.Minute)
			defer cancel()
			_, _ = runpodDestroy(ctx, opts.RunpodVarFile)
		}
	}()

	for _, id := range m.ordered {
		if r := m.byID[id]; r != nil && r.Running {
			return nil, fmt.Errorf("a run is already in progress (%s)", id[:8])
		}
	}

	id := newRunID()
	cleanups := append([]string(nil), configCleanups...)
	rec := &runRecord{
		ID:                    id,
		ConfigRel:             relDisplay,
		Started:               time.Now(),
		Running:               true,
		RunTarget:             opts.RunTarget,
		RunpodDestroyOnExit:   opts.RunpodDestroyOnExit,
		RunpodVarFile:         opts.RunpodVarFile,
		configCleanups:        cleanups,
		serverlessTomlOverlay: strings.TrimSpace(serverlessTomlOverlay),
	}
	if applyLog != "" {
		rec.logBuf.WriteString("=== runpod: OpenTofu apply ===\n")
		rec.logBuf.WriteString(applyLog)
		rec.logBuf.WriteByte('\n')
	} else if opts.RunTarget == "runpod" && opts.RunpodSkipApply {
		if warmIP != "" {
			rec.logBuf.WriteString("=== runpod: warm target — skipping OpenTofu apply ===\n")
		} else {
			rec.logBuf.WriteString("=== runpod: skipped OpenTofu apply (using existing terraform state) ===\n")
		}
	}

	if opts.RunTarget == "runpod_serverless" {
		ep := strings.TrimSpace(opts.RunpodServerlessEndpointID)
		if ep == "" {
			ep = runpodServerlessEndpointID()
		}
		if ep == "" {
			return nil, fmt.Errorf("runpod_serverless: no endpoint id — enter Serverless endpoint id in the training wizard (step 2) or configure it for the WUI server process")
		}
		if runpodServerlessQueueAPIKey() == "" {
			return nil, fmt.Errorf("runpod_serverless: no queue API credentials — configure RUNPOD_TOKEN_END and/or RUNPOD_API_KEY or RUNPOD_TOKEN for the WUI process")
		}
		rec.logBuf.WriteString("=== runpod serverless: POST /run (async training job) ===\n")
		ctxSub, cancelSub := context.WithTimeout(context.Background(), 5*time.Minute)
		payload := buildQMWServerlessTrainInputV1(relDisplay, extraEnv, rec.serverlessTomlOverlay)
		if pb, err := json.MarshalIndent(payload, "", "  "); err == nil {
			rec.logBuf.WriteString("=== runpod serverless payload ===\n")
			rec.logBuf.Write(pb)
			rec.logBuf.WriteByte('\n')
		}
		out, code, raw, subErr := RunpodServerlessRunAsync(ctxSub, ep, payload)
		cancelSub()
		if subErr != nil {
			return nil, fmt.Errorf("runpod serverless /run: %w", subErr)
		}
		if code >= 400 {
			return nil, fmt.Errorf("runpod serverless /run failed: HTTP %d: %s", code, string(raw))
		}
		jobID, _ := out["id"].(string)
		if strings.TrimSpace(jobID) == "" {
			return nil, fmt.Errorf("runpod serverless /run: no job id in response: %s", string(raw))
		}
		rec.ServerlessJobID = jobID
		rec.ServerlessEndpointID = ep
		rec.logBuf.WriteString(fmt.Sprintf("=== runpod serverless: job id %s ===\n", jobID))

		m.byID[id] = rec
		m.ordered = append([]string{id}, m.ordered...)
		if len(m.ordered) > 32 {
			m.ordered = m.ordered[:32]
		}
		go m.pollServerlessJob(id, ep, jobID, cleanups)
		wsHub.broadcast(id, map[string]any{
			"type":   "lifecycle",
			"run_id": id,
			"state":  "started",
			"ts":     time.Now().UTC().Format(time.RFC3339),
		})
		registered = true
		return rec, nil
	}

	if useNativeTrainingEngineGRPC(opts) {
		cfg, err := TrainingTOMLToProto(absConfig, id, repoRoot)
		if err != nil {
			return nil, fmt.Errorf("map training TOML to gRPC config: %w", err)
		}
		// Python runs populate TotalEpochs from log lines; gRPC streams never hit that path — set from TOML so
		// /api/node/health and the sticky bar show "epoch / total" instead of looking hung at high epoch.
		rec.TotalEpochs = int(cfg.GetEpochs())
		ctxTrain, cancelTrain := context.WithCancel(context.Background())
		rec.procExited = make(chan struct{})
		rec.trainCancel = cancelTrain
		rec.logBuf.WriteString("=== training: native gRPC TrainingEngineService (C++ engine on " + trainingGRPCAddressResolved() + ") ===\n")
		m.byID[id] = rec
		m.ordered = append([]string{id}, m.ordered...)
		if len(m.ordered) > 32 {
			m.ordered = m.ordered[:32]
		}
		go m.runNativeGRPCTraining(ctxTrain, id, cfg, rec)
		wsHub.broadcast(id, map[string]any{
			"type":   "lifecycle",
			"run_id": id,
			"state":  "started",
			"ts":     time.Now().UTC().Format(time.RFC3339),
		})
		registered = true
		return rec, nil
	}

	if trainingRuntimeModeRaw() == "auto" {
		_, reason := trainingEnginePickLocal()
		rec.logBuf.WriteString("=== training: " + reason + " ===\n")
	}

	runtimeEnv := wuiRuntimeEnvForPython()
	trainEnv := append(append([]string(nil), runtimeEnv...), extraEnv...)

	var cmd *exec.Cmd
	if opts.RunTarget == "runpod" && opts.RunpodTrainOnPod {
		ipCtx, cancelIP := context.WithTimeout(context.Background(), 12*time.Minute)
		defer cancelIP()
		var ip string
		var waitErr error
		if warmIP != "" {
			ip = warmIP
			rec.logBuf.WriteString(fmt.Sprintf("=== runpod: SSH host from warm target (%s) ===\n", ip))
		} else {
			ip, waitErr = waitRunpodPublicIP(ipCtx)
			if waitErr != nil {
				return nil, waitErr
			}
		}
		user := runpodSSHUser()
		if warmSSHUser != "" {
			user = warmSSHUser
		}
		rdir := runpodRemoteDir()
		key := runpodSSHKeyPath()
		if !runpodToolOK("ssh") || !runpodToolOK("tar") {
			return nil, fmt.Errorf("RunPod remote training needs ssh and tar on PATH (install OpenSSH client; Windows: Optional Features → OpenSSH Client)")
		}
		syncCtx, cancelSync := context.WithTimeout(context.Background(), 45*time.Minute)
		defer cancelSync()
		rec.logBuf.WriteString(fmt.Sprintf("=== runpod: syncing repo to %s@%s:%s (tar over ssh) ===\n", user, ip, rdir))
		if err := runpodSyncRepo(syncCtx, ip, user, key, rdir); err != nil {
			return nil, fmt.Errorf("runpod sync: %w", err)
		}
		remoteConfigRel := relDisplay
		if len(cleanups) > 0 {
			if !runpodToolOK("scp") {
				return nil, fmt.Errorf("scp not on PATH: required to upload generated config for this run")
			}
			tag := newRunID()
			remoteConfigRel = fmt.Sprintf("configs/training/.wui_%s.toml", tag[:12])
			remotePath := rdir + "/" + remoteConfigRel
			scpCtx, cancelScp := context.WithTimeout(context.Background(), 10*time.Minute)
			defer cancelScp()
			rec.logBuf.WriteString(fmt.Sprintf("=== runpod: uploading config to %s ===\n", remotePath))
			if err := runpodSCPLocalToRemote(scpCtx, ip, user, key, absConfig, remotePath); err != nil {
				return nil, fmt.Errorf("runpod scp config: %w", err)
			}
		}
		wuiStopRel := ".wui/stop_" + id
		rTrainCmd, rCmdErr := runpodRemoteTrainCmd(ip, user, key, rdir, remoteConfigRel, trainEnv, wuiStopRel)
		if rCmdErr != nil {
			return nil, rCmdErr
		}
		cmd = rTrainCmd
		rec.wuiStopFile = strings.TrimRight(rdir, "/") + "/" + wuiStopRel
		rec.runpodCoopHost = ip
		rec.runpodCoopUser = user
		rec.runpodCoopKeyPath = key
		rec.logBuf.WriteString(fmt.Sprintf("=== training: remote python -u -m qminiwasm.engine (ssh %s@%s) ===\n", user, ip))
	} else {
		wuiDir := filepath.Join(repoRoot, ".wui")
		if mkErr := os.MkdirAll(wuiDir, 0o755); mkErr != nil {
			return nil, fmt.Errorf("wui stop dir: %w", mkErr)
		}
		stopAbs := filepath.Join(wuiDir, "stop_"+id)
		_ = os.Remove(stopAbs)
		rec.wuiStopFile = stopAbs
		rec.logBuf.WriteString("=== training: python -u -m qminiwasm.engine (local WUI host) ===\n")
		cmd = exec.Command(pythonExe, "-u", "-m", "qminiwasm.engine", "--config", absConfig, "--wui-stop-file", stopAbs)
		cmd.Dir = repoRoot
		cmd.Env = append(os.Environ(), trainEnv...)
	}

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		return nil, err
	}
	rec.cmd = cmd
	rec.procExited = make(chan struct{})
	m.byID[id] = rec
	m.ordered = append([]string{id}, m.ordered...)
	if len(m.ordered) > 32 {
		m.ordered = m.ordered[:32]
	}

	go m.pump(id, stdout, "stdout")
	go m.pump(id, stderr, "stderr")
	go m.wait(id)
	wsHub.broadcast(id, map[string]any{
		"type":   "lifecycle",
		"run_id": id,
		"state":  "started",
		"ts":     time.Now().UTC().Format(time.RFC3339),
	})

	registered = true
	return rec, nil
}

func (m *manager) pump(id string, r io.Reader, stream string) {
	buf := make([]byte, 4096)
	for {
		n, err := r.Read(buf)
		if n > 0 {
			m.appendLog(id, buf[:n], stream)
		}
		if err != nil {
			return
		}
	}
}

func (m *manager) appendLog(id string, p []byte, stream string) {
	m.mu.Lock()
	rec := m.byID[id]
	m.mu.Unlock()
	if rec == nil {
		return
	}
	rec.logMu.Lock()
	rec.logBuf.Write(p)
	if rec.cmd != nil {
		m.processTelemetryLinesLocked(id, rec, p, stream)
	}
	rec.logMu.Unlock()
}

func (m *manager) processTelemetryLinesLocked(id string, rec *runRecord, p []byte, stream string) {
	var sb *strings.Builder
	if stream == "stderr" {
		sb = &rec.stderrLineBuf
	} else {
		sb = &rec.stdoutLineBuf
	}
	sb.Write(p)
	for {
		s := sb.String()
		i := strings.IndexByte(s, '\n')
		if i < 0 {
			return
		}
		line := strings.TrimRight(strings.TrimSpace(s[:i]), "\r")
		remaining := s[i+1:]
		sb.Reset()
		sb.WriteString(remaining)
		m.handleLogLine(id, line, stream)
	}
}

func (m *manager) flushTelemetryLinesLocked(id string, rec *runRecord) {
	if rec.stdoutLineBuf.Len() > 0 {
		line := strings.TrimRight(strings.TrimSpace(rec.stdoutLineBuf.String()), "\r")
		rec.stdoutLineBuf.Reset()
		m.handleLogLine(id, line, "stdout")
	}
	if rec.stderrLineBuf.Len() > 0 {
		line := strings.TrimRight(strings.TrimSpace(rec.stderrLineBuf.String()), "\r")
		rec.stderrLineBuf.Reset()
		m.handleLogLine(id, line, "stderr")
	}
}

// parsePythonTelemetryLine scans one log line for qmw_* / metric patterns (stdout or stderr).
func (m *manager) parsePythonTelemetryLine(id, line string) {
	if line == "" {
		return
	}
	if mm := metricCanonicalRe.FindStringSubmatch(line); len(mm) == 5 {
		epoch, _ := strconv.ParseFloat(mm[1], 64)
		meanLoss, _ := strconv.ParseFloat(mm[2], 64)
		meanReturn, _ := strconv.ParseFloat(mm[3], 64)
		meanMSE, _ := strconv.ParseFloat(mm[4], 64)
		wsHub.broadcast(id, map[string]any{
			"type":             "metric",
			"run_id":           id,
			"ts":               time.Now().UTC().Format(time.RFC3339),
			"epoch":            epoch,
			"mean_loss":        meanLoss,
			"mean_return":      meanReturn,
			"mean_mse":         meanMSE,
			"line":             line,
			"telemetry_source": telemetrySourcePythonVNV,
		})
		m.mu.Lock()
		if rec := m.byID[id]; rec != nil {
			rec.LastEpoch = int(epoch)
			rec.LastMeanLoss = meanLoss
			rec.LastMeanReturn = meanReturn
			rec.LastMeanMSE = meanMSE
		}
		m.mu.Unlock()
		return
	}
	if mm := metricLineRe.FindStringSubmatch(line); len(mm) == 5 {
		epoch, _ := strconv.ParseFloat(mm[1], 64)
		meanLoss, _ := strconv.ParseFloat(mm[2], 64)
		meanReturn, _ := strconv.ParseFloat(mm[3], 64)
		meanMSE, _ := strconv.ParseFloat(mm[4], 64)
		wsHub.broadcast(id, map[string]any{
			"type":             "metric",
			"run_id":           id,
			"ts":               time.Now().UTC().Format(time.RFC3339),
			"epoch":            epoch,
			"mean_loss":        meanLoss,
			"mean_return":      meanReturn,
			"mean_mse":         meanMSE,
			"line":             line,
			"telemetry_source": telemetrySourcePythonVNV,
		})
		m.mu.Lock()
		if rec := m.byID[id]; rec != nil {
			rec.LastEpoch = int(epoch)
			rec.LastMeanLoss = meanLoss
			rec.LastMeanReturn = meanReturn
			rec.LastMeanMSE = meanMSE
		}
		m.mu.Unlock()
	}
	if pm := epochProgressRe.FindStringSubmatch(line); len(pm) == 3 {
		cur, _ := strconv.Atoi(pm[1])
		tot, _ := strconv.Atoi(pm[2])
		m.mu.Lock()
		if rec := m.byID[id]; rec != nil {
			rec.LastEpoch = cur
			rec.TotalEpochs = tot
		}
		m.mu.Unlock()
	}
	if qm := pruneStatsRe.FindStringSubmatch(line); len(qm) == 3 {
		base, _ := strconv.Atoi(qm[1])
		pruned, _ := strconv.Atoi(qm[2])
		m.mu.Lock()
		if rec := m.byID[id]; rec != nil {
			rec.PrunedFromNodes = base
			rec.PrunedToNodes = pruned
		}
		m.mu.Unlock()
	}
	if lm := qaoaLoadRe.FindStringSubmatch(line); len(lm) == 3 {
		sim, _ := strconv.ParseFloat(lm[1], 64)
		qpu, _ := strconv.ParseFloat(lm[2], 64)
		m.mu.Lock()
		if rec := m.byID[id]; rec != nil {
			rec.QAOASimMS = sim
			rec.QPUEstMS = qpu
		}
		m.mu.Unlock()
	}
	if em := enclaveTelemetryRe.FindStringSubmatch(line); len(em) == 5 {
		est, _ := strconv.ParseFloat(em[1], 64)
		capMB, _ := strconv.ParseFloat(em[2], 64)
		tier := em[3]
		u64, _ := strconv.Atoi(em[4])
		wsHub.broadcast(id, map[string]any{
			"type":              "enclave_telemetry",
			"run_id":            id,
			"ts":                time.Now().UTC().Format(time.RFC3339),
			"estimated_tpem_mb": est,
			"tier_cap_mb":       capMB,
			"enclave_tier":      tier,
			"use_memory64":      u64,
			"line":              line,
			"telemetry_source":  telemetrySourcePythonVNV,
		})
	}
	if xm := xpuMemTrainingSetupRe.FindStringSubmatch(line); len(xm) == 5 {
		bs, _ := strconv.Atoi(xm[2])
		ts, _ := strconv.Atoi(xm[3])
		dw, _ := strconv.Atoi(xm[4])
		wsHub.broadcast(id, map[string]any{
			"type":               "xpu_mem",
			"run_id":             id,
			"ts":                 time.Now().UTC().Format(time.RFC3339),
			"phase":              "training_setup",
			"device":             xm[1],
			"batch_size":         bs,
			"train_samples":      ts,
			"dataloader_workers": dw,
			"line":               line,
			"telemetry_source":   telemetrySourcePythonVNV,
		})
	}
	if xm := xpuMemEpochRe.FindStringSubmatch(line); len(xm) == 10 {
		ep, _ := strconv.Atoi(xm[2])
		etot, _ := strconv.Atoi(xm[3])
		batches, _ := strconv.Atoi(xm[4])
		xidx, _ := strconv.Atoi(xm[5])
		ab, _ := strconv.ParseInt(xm[6], 10, 64)
		mb, _ := strconv.ParseInt(xm[7], 10, 64)
		ami, _ := strconv.ParseFloat(xm[8], 64)
		mami, _ := strconv.ParseFloat(xm[9], 64)
		wsHub.broadcast(id, map[string]any{
			"type":                "xpu_mem",
			"run_id":              id,
			"ts":                  time.Now().UTC().Format(time.RFC3339),
			"phase":               xm[1],
			"epoch":               ep,
			"epochs_total":        etot,
			"batches":             batches,
			"xpu_index":           xidx,
			"allocated_bytes":     ab,
			"max_allocated_bytes": mb,
			"allocated_mib":       ami,
			"max_allocated_mib":   mami,
			"line":                line,
			"telemetry_source":    telemetrySourcePythonVNV,
		})
	}
	if tt := trainThroughputRe.FindStringSubmatch(line); len(tt) >= 12 {
		ep, _ := strconv.Atoi(tt[1])
		etot, _ := strconv.Atoi(tt[2])
		wall, _ := strconv.ParseFloat(tt[3], 64)
		batches, _ := strconv.Atoi(tt[4])
		samples, _ := strconv.Atoi(tt[5])
		bs, _ := strconv.Atoi(tt[6])
		sps, _ := strconv.ParseFloat(tt[7], 64)
		bps, _ := strconv.ParseFloat(tt[8], 64)
		dw, _ := strconv.Atoi(tt[9])
		cs, _ := strconv.ParseFloat(tt[10], 64)
		payload := map[string]any{
			"type":               "train_throughput",
			"run_id":             id,
			"ts":                 time.Now().UTC().Format(time.RFC3339),
			"epoch":              ep,
			"epochs_total":       etot,
			"wall_s":             wall,
			"batches":            batches,
			"samples":            samples,
			"batch_size":         bs,
			"samples_per_s":      sps,
			"batches_per_s":      bps,
			"dataloader_workers": dw,
			"cascade_s":          cs,
			"line":               line,
			"telemetry_source":   telemetrySourcePythonVNV,
		}
		if len(tt) > 11 && tt[11] != "" {
			if rss, err := strconv.ParseFloat(tt[11], 64); err == nil {
				payload["host_rss_mib"] = rss
			}
		}
		wsHub.broadcast(id, payload)
	}
	if rm := routingTelemetryRe.FindStringSubmatch(line); len(rm) == 4 {
		st, _ := strconv.Atoi(rm[1])
		assign, _ := strconv.ParseFloat(rm[2], 64)
		budget, _ := strconv.ParseFloat(rm[3], 64)
		m.mu.Lock()
		if rec := m.byID[id]; rec != nil {
			rec.LastRoutingState = st
			rec.LastAssignmentMS = assign
			rec.RoutingBudgetMS = budget
		}
		m.mu.Unlock()
		wsHub.broadcast(id, map[string]any{
			"type":             "routing_telemetry",
			"run_id":           id,
			"ts":               time.Now().UTC().Format(time.RFC3339),
			"routing_state":    st,
			"assignment_ms":    assign,
			"budget_ms":        budget,
			"line":             line,
			"telemetry_source": telemetrySourcePythonVNV,
		})
	}
	if hm := routingHandoffRe.FindStringSubmatch(line); len(hm) == 7 {
		fromSt, _ := strconv.Atoi(hm[1])
		toSt, _ := strconv.Atoi(hm[2])
		assign, _ := strconv.ParseFloat(hm[3], 64)
		budget, _ := strconv.ParseFloat(hm[4], 64)
		reason := hm[5]
		comb, _ := strconv.Atoi(hm[6])
		m.mu.Lock()
		if rec := m.byID[id]; rec != nil {
			rec.LastRoutingState = toSt
			rec.LastAssignmentMS = assign
			rec.RoutingBudgetMS = budget
		}
		m.mu.Unlock()
		wsHub.broadcast(id, map[string]any{
			"type":               "routing_handoff",
			"run_id":             id,
			"ts":                 time.Now().UTC().Format(time.RFC3339),
			"from_state":         fromSt,
			"to_state":           toSt,
			"assignment_ms":      assign,
			"budget_ms":          budget,
			"reason":             reason,
			"combinatorial_wall": comb,
			"line":               line,
			"telemetry_source":   telemetrySourcePythonVNV,
		})
	}
	if bm := backendStatusRe.FindStringSubmatch(line); len(bm) == 14 {
		syclActive := bm[9] == "1"
		dpctlCount, _ := strconv.Atoi(strings.TrimSpace(bm[13]))
		m.mu.Lock()
		if rec := m.byID[id]; rec != nil {
			rec.BackendRequested = bm[2]
			rec.BackendSelected = bm[3]
			rec.BackendReasonCode = bm[4]
			rec.XPUSupportClass = bm[6]
			rec.SYCLActive = syclActive
			rec.SYCLBackend = bm[10]
			rec.SYCLDevice = bm[11]
			rec.SYCLFallback = bm[12]
			rec.SYCLDPCTLCount = dpctlCount
		}
		m.mu.Unlock()
		wsHub.broadcast(id, map[string]any{
			"type":                  "backend_status",
			"run_id":                id,
			"ts":                    time.Now().UTC().Format(time.RFC3339),
			"requested_accelerator": bm[2],
			"selected_device":       bm[3],
			"reason_code":           bm[4],
			"xpu_support_class":     bm[6],
			"sycl_active":           syclActive,
			"sycl_backend":          bm[10],
			"sycl_device":           bm[11],
			"sycl_fallback":         bm[12],
			"sycl_dpctl_count":      dpctlCount,
			"line":                  line,
			"telemetry_source":      telemetrySourcePythonVNV,
		})
	}
}

func (m *manager) handleLogLine(id, line, stream string) {
	if line == "" {
		return
	}
	m.parsePythonTelemetryLine(id, line)
	if stream == "stdout" {
		return
	}
	if strings.Contains(line, alertXPU) {
		m.mu.Lock()
		if rec := m.byID[id]; rec != nil {
			rec.XPUFallbackSeen = true
		}
		m.mu.Unlock()
		wsHub.broadcast(id, map[string]any{
			"type":     "alert",
			"run_id":   id,
			"ts":       time.Now().UTC().Format(time.RFC3339),
			"severity": "critical",
			"code":     "xpu_fallback_cpu",
			"message":  line,
		})
	}
	if strings.Contains(strings.ToLower(line), strings.ToLower(alertWASM)) {
		m.mu.Lock()
		if rec := m.byID[id]; rec != nil {
			rec.WasmMockSeen = true
		}
		m.mu.Unlock()
		wsHub.broadcast(id, map[string]any{
			"type":     "alert",
			"run_id":   id,
			"ts":       time.Now().UTC().Format(time.RFC3339),
			"severity": "critical",
			"code":     "wasm_mock_fallback",
			"message":  line,
		})
	}
}

func (m *manager) wait(id string) {
	m.mu.Lock()
	rec := m.byID[id]
	if rec == nil {
		m.mu.Unlock()
		return
	}
	cmd := rec.cmd
	procDone := rec.procExited
	runTarget := rec.RunTarget
	destroyPod := rec.RunpodDestroyOnExit
	runpodVF := rec.RunpodVarFile
	configCleanups := rec.configCleanups
	rec.configCleanups = nil
	m.mu.Unlock()

	if cmd == nil {
		return
	}
	err := cmd.Wait()
	if procDone != nil {
		close(procDone)
	}
	var localWuiStop string
	m.mu.Lock()
	rec = m.byID[id]
	if rec != nil {
		rec.logMu.Lock()
		m.flushTelemetryLinesLocked(id, rec)
		rec.logMu.Unlock()
		if rec.wuiStopFile != "" && rec.runpodCoopHost == "" {
			localWuiStop = rec.wuiStopFile
		}
	}
	m.mu.Unlock()
	if localWuiStop != "" {
		_ = os.Remove(localWuiStop)
	}
	for _, p := range configCleanups {
		if p != "" {
			if remErr := os.Remove(p); remErr != nil && !errors.Is(remErr, os.ErrNotExist) {
				m.appendLog(id, []byte("\n=== cleanup warning: failed to remove temp config "+p+": "+remErr.Error()+" ===\n"), "stderr")
			}
		}
	}
	exitCode := 0
	var waitErrLog string
	var waitExitLog string
	m.mu.Lock()
	rec = m.byID[id]
	if rec != nil {
		rec.Running = false
		if err != nil {
			rec.Error = true
			waitErrLog = "\n=== process wait error: " + err.Error() + " ===\n"
			if x, ok := err.(*exec.ExitError); ok {
				rec.ExitCode = x.ExitCode()
				exitCode = x.ExitCode()
				waitExitLog = fmt.Sprintf("=== process exited with code %d (run_target=%s) ===\n", exitCode, runTarget)
			} else {
				rec.ExitCode = -1
				exitCode = -1
				waitExitLog = fmt.Sprintf("=== process exited abnormally (run_target=%s) ===\n", runTarget)
			}
		} else {
			rec.ExitCode = 0
		}
	} else if err != nil {
		if x, ok := err.(*exec.ExitError); ok {
			exitCode = x.ExitCode()
		} else {
			exitCode = -1
		}
	}
	m.mu.Unlock()
	if waitErrLog != "" {
		m.appendLog(id, []byte(waitErrLog), "stderr")
	}
	if waitExitLog != "" {
		m.appendLog(id, []byte(waitExitLog), "stderr")
	}

	wsHub.broadcast(id, map[string]any{
		"type":      "lifecycle",
		"run_id":    id,
		"state":     "finished",
		"ts":        time.Now().UTC().Format(time.RFC3339),
		"exit_code": exitCode,
	})

	if runTarget == "runpod" && destroyPod {
		ctx, cancel := context.WithTimeout(context.Background(), 20*time.Minute)
		defer cancel()
		out, derr := runpodDestroy(ctx, runpodVF)
		m.appendLog(id, []byte("\n=== runpod: OpenTofu destroy ===\n"), "stderr")
		m.appendLog(id, []byte(out), "stderr")
		if derr != nil {
			m.appendLog(id, []byte("\n=== runpod destroy error: "+derr.Error()+" ===\n"), "stderr")
		}
	}
}

func (m *manager) logChunk(id string, offset int64) (text string, next int64, running bool, err error) {
	m.mu.Lock()
	rec := m.byID[id]
	m.mu.Unlock()
	if rec == nil {
		return "", 0, false, errors.New("run not found")
	}
	rec.logMu.Lock()
	b := rec.logBuf.Bytes()
	if offset < 0 {
		offset = 0
	}
	if offset > int64(len(b)) {
		offset = int64(len(b))
	}
	chunk := string(b[offset:])
	rec.logMu.Unlock()
	return chunk, int64(len(b)), rec.Running, nil
}

func (m *manager) exitCode(id string) int {
	m.mu.Lock()
	defer m.mu.Unlock()
	if r := m.byID[id]; r != nil {
		return r.ExitCode
	}
	return 0
}

func (m *manager) list() []map[string]any {
	m.mu.Lock()
	defer m.mu.Unlock()
	var out []map[string]any
	for _, id := range m.ordered {
		r := m.byID[id]
		if r == nil {
			continue
		}
		row := map[string]any{
			"id":                     r.ID,
			"config":                 r.ConfigRel,
			"started":                r.Started.Format(time.RFC3339),
			"running":                r.Running,
			"exit_code":              r.ExitCode,
			"error":                  r.Error,
			"run_target":             r.RunTarget,
			"runpod_destroy_on_exit": r.RunpodDestroyOnExit,
			"runpod_var_file":        r.RunpodVarFile,
		}
		if r.ServerlessJobID != "" {
			row["serverless_job_id"] = r.ServerlessJobID
		}
		if r.ServerlessEndpointID != "" {
			row["serverless_endpoint_id"] = r.ServerlessEndpointID
		}
		if r.LastRoutingState > 0 {
			row["routing_state"] = r.LastRoutingState
			row["routing_assignment_ms"] = r.LastAssignmentMS
			row["routing_budget_ms"] = r.RoutingBudgetMS
		}
		out = append(out, row)
	}
	return out
}

func (m *manager) activeRunSummary() map[string]any {
	m.mu.Lock()
	defer m.mu.Unlock()
	for _, id := range m.ordered {
		r := m.byID[id]
		if r == nil || !r.Running {
			continue
		}
		s := map[string]any{
			"id":                    r.ID,
			"config":                r.ConfigRel,
			"run_target":            r.RunTarget,
			"epoch":                 r.LastEpoch,
			"total_epochs":          r.TotalEpochs,
			"mean_loss":             r.LastMeanLoss,
			"mean_return":           r.LastMeanReturn,
			"mean_mse":              r.LastMeanMSE,
			"xpu_fallback":          r.XPUFallbackSeen,
			"wasm_mock":             r.WasmMockSeen,
			"backend_requested":     r.BackendRequested,
			"backend_selected":      r.BackendSelected,
			"backend_reason":        r.BackendReasonCode,
			"xpu_support_class":     r.XPUSupportClass,
			"sycl_active":           r.SYCLActive,
			"sycl_backend":          r.SYCLBackend,
			"sycl_device":           r.SYCLDevice,
			"sycl_fallback":         r.SYCLFallback,
			"sycl_dpctl_count":      r.SYCLDPCTLCount,
			"pruned_from_nodes":     r.PrunedFromNodes,
			"pruned_to_nodes":       r.PrunedToNodes,
			"qaoa_sim_ms":           r.QAOASimMS,
			"qpu_est_ms":            r.QPUEstMS,
			"routing_state":         r.LastRoutingState,
			"routing_assignment_ms": r.LastAssignmentMS,
			"routing_budget_ms":     r.RoutingBudgetMS,
		}
		if r.ServerlessJobID != "" {
			s["serverless_job_id"] = r.ServerlessJobID
		}
		return s
	}
	return nil
}

func (m *manager) stop(id string, force bool) error {
	m.mu.Lock()
	rec := m.byID[id]
	if rec == nil || !rec.Running {
		m.mu.Unlock()
		return errors.New("run not active")
	}
	if rec.RunTarget == "runpod_serverless" && rec.ServerlessJobID != "" && rec.ServerlessEndpointID != "" {
		jobID := rec.ServerlessJobID
		ep := rec.ServerlessEndpointID
		m.mu.Unlock()
		ctx, cancel := context.WithTimeout(context.Background(), 45*time.Second)
		defer cancel()
		if err := RunpodServerlessCancel(ctx, ep, jobID); err != nil {
			return err
		}
		wsHub.broadcast(id, map[string]any{
			"type":   "lifecycle",
			"run_id": id,
			"state":  "stop_requested",
			"mode":   map[string]any{"force": force, "serverless_cancel": true},
			"ts":     time.Now().UTC().Format(time.RFC3339),
		})
		return nil
	}
	if rec.cmd == nil {
		trainCancel := rec.trainCancel
		rec.trainCancel = nil
		m.mu.Unlock()
		if trainCancel == nil {
			return errors.New("run not active")
		}
		if force {
			wsHub.broadcast(id, map[string]any{
				"type":   "lifecycle",
				"run_id": id,
				"state":  "stop_requested",
				"mode":   map[string]any{"force": true, "grpc": true},
				"ts":     time.Now().UTC().Format(time.RFC3339),
			})
		} else {
			wsHub.broadcast(id, map[string]any{
				"type":   "lifecycle",
				"run_id": id,
				"state":  "stop_requested",
				"mode":   map[string]any{"force": false, "graceful": true, "grpc": true},
				"ts":     time.Now().UTC().Format(time.RFC3339),
			})
		}
		// Ask the C++ engine to stop first so it can emit checkpoint + completed telemetry; then drain
		// StreamTelemetry on a context that is not cancelled by trainCancel (see runNativeGRPCTraining).
		sctx, scancel := context.WithTimeout(context.Background(), 15*time.Second)
		stopErr := m.grpcCallStopTraining(sctx, id)
		scancel()
		if stopErr != nil {
			m.appendLogSubprocessAware(id, []byte("\n=== gRPC StopTraining: "+stopErr.Error()+" ===\n"), "stderr")
		} else {
			m.appendLogSubprocessAware(id, []byte("\n=== gRPC StopTraining: ok (draining telemetry stream) ===\n"), "stderr")
		}
		if force {
			time.Sleep(400 * time.Millisecond)
			m.mu.Lock()
			if r := m.byID[id]; r != nil && r.nativeStreamCancel != nil {
				r.nativeStreamCancel()
			}
			m.mu.Unlock()
		}
		trainCancel()
		return nil
	}
	if rec.cmd.Process == nil {
		m.mu.Unlock()
		return errors.New("run not active")
	}
	proc := rec.cmd.Process
	procDone := rec.procExited
	m.mu.Unlock()

	if force {
		wsHub.broadcast(id, map[string]any{
			"type":   "lifecycle",
			"run_id": id,
			"state":  "stop_requested",
			"mode":   map[string]any{"force": true},
			"ts":     time.Now().UTC().Format(time.RFC3339),
		})
		return proc.Kill()
	}

	wsHub.broadcast(id, map[string]any{
		"type":   "lifecycle",
		"run_id": id,
		"state":  "stop_requested",
		"mode":   map[string]any{"force": false, "graceful": true},
		"ts":     time.Now().UTC().Format(time.RFC3339),
	})

	if err := proc.Signal(os.Interrupt); err != nil {
		hint := ""
		if runtime.GOOS == "windows" {
			hint = " On Windows, graceful SIGINT often fails for detached GUI-spawned children; prefer running the WUI from a console or use Stop after a full epoch."
		}
		m.appendLog(
			id,
			[]byte(fmt.Sprintf(
				"\n=== WUI: could not deliver graceful interrupt (%v); hard killing.%s ===\n",
				err,
				hint,
			)),
			"stderr",
		)
		return forceKillAndReturn(proc)
	}
	if procDone != nil {
		go func() {
			timer := time.NewTimer(2 * time.Minute)
			defer timer.Stop()
			select {
			case <-procDone:
				return
			case <-timer.C:
				_ = proc.Kill()
			}
		}()
	}
	return nil
}

func (m *manager) cooperativeStop(id string) error {
	m.mu.Lock()
	rec := m.byID[id]
	if rec == nil || !rec.Running {
		m.mu.Unlock()
		return errors.New("run not active")
	}
	host := strings.TrimSpace(rec.runpodCoopHost)
	user := strings.TrimSpace(rec.runpodCoopUser)
	keyPath := strings.TrimSpace(rec.runpodCoopKeyPath)
	stopPath := strings.TrimSpace(rec.wuiStopFile)
	trainCancel := rec.trainCancel
	cmd := rec.cmd
	runTarget := rec.RunTarget
	m.mu.Unlock()

	if trainCancel != nil && cmd == nil {
		return errors.New("cooperative stop file is not used for native gRPC training; use Stop (graceful) for the C++ engine")
	}
	if stopPath == "" {
		return errors.New("cooperative stop is not configured for this run (e.g. RunPod serverless uses cancel only)")
	}
	if runTarget == "runpod_serverless" {
		return errors.New("cooperative stop file is not available for RunPod serverless; use Stop to cancel the job")
	}

	if host != "" {
		ctx, cancel := context.WithTimeout(context.Background(), 45*time.Second)
		defer cancel()
		if err := runpodRemoteTouchStopFile(ctx, host, user, keyPath, stopPath); err != nil {
			return err
		}
		wsHub.broadcast(id, map[string]any{
			"type":   "lifecycle",
			"run_id": id,
			"state":  "cooperative_stop_requested",
			"mode":   map[string]any{"cooperative_file": true, "remote": true},
			"ts":     time.Now().UTC().Format(time.RFC3339),
		})
		return nil
	}
	if cmd == nil {
		return errors.New("run not active")
	}
	if err := os.WriteFile(stopPath, []byte("1\n"), 0o644); err != nil {
		return fmt.Errorf("write cooperative stop file: %w", err)
	}
	wsHub.broadcast(id, map[string]any{
		"type":   "lifecycle",
		"run_id": id,
		"state":  "cooperative_stop_requested",
		"mode":   map[string]any{"cooperative_file": true},
		"ts":     time.Now().UTC().Format(time.RFC3339),
	})
	return nil
}

func forceKillAndReturn(proc *os.Process) error {
	if proc == nil {
		return errors.New("no process")
	}
	return proc.Kill()
}
