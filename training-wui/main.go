// Command training-wui is a small web UI to list training TOML configs and run
// `python -m engine --config <path>` from the repository root.
package main

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"embed"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
)

//go:embed web/*
var webFS embed.FS

var (
	repoRoot   string
	pythonExe  string
	runManager = newManager()
)

func main() {
	addr := ":8765"
	if v := os.Getenv("TRAINING_WUI_ADDR"); v != "" {
		addr = v
	}
	root := "."
	if v := os.Getenv("TRAINING_WUI_ROOT"); v != "" {
		root = v
	}
	py := "python"
	if v := os.Getenv("TRAINING_WUI_PYTHON"); v != "" {
		py = v
	}

	args := os.Args[1:]
	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "-addr":
			if i+1 < len(args) {
				i++
				addr = args[i]
			}
		case "-root":
			if i+1 < len(args) {
				i++
				root = args[i]
			}
		case "-python":
			if i+1 < len(args) {
				i++
				py = args[i]
			}
		}
	}

	abs, err := filepath.Abs(root)
	if err != nil {
		log.Fatal(err)
	}
	repoRoot = filepath.Clean(abs)
	pythonExe = py

	trainingDir := filepath.Join(repoRoot, "configs", "training")
	if st, err := os.Stat(trainingDir); err != nil || !st.IsDir() {
		log.Printf("warning: %q missing or not a directory (set -root to repo root)", trainingDir)
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/api/configs", handleConfigs)
	mux.HandleFunc("/api/preflight", handlePreflight)
	mux.HandleFunc("/api/lr/auto", handleAutoLR)
	mux.HandleFunc("/api/runs", handleRunsCollection)
	mux.HandleFunc("/api/runs/build", handleRunBuild)
	mux.HandleFunc("/api/runs/", handleRunsItem)

	sub, err := fs.Sub(webFS, "web")
	if err != nil {
		log.Fatal(err)
	}
	mux.Handle("/", noCache(http.FileServer(http.FS(sub))))

	log.Printf("training-wui listening on %s (repo root %s, python %q)", addr, repoRoot, pythonExe)
	log.Fatal(http.ListenAndServe(addr, withCORS(mux)))
}

func withCORS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
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

func handleRunsCollection(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{"runs": runManager.list()})
	case http.MethodPost:
		var body struct {
			Config string `json:"config"`
		}
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			jsonErr(w, http.StatusBadRequest, "invalid JSON")
			return
		}
		abs, err := resolveTrainingConfig(repoRoot, body.Config)
		if err != nil {
			jsonErr(w, http.StatusBadRequest, err.Error())
			return
		}
		run, err := runManager.start(abs, body.Config)
		if err != nil {
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

type buildRunRequest struct {
	Name               string  `json:"name"`
	Accelerator        string  `json:"accelerator"`
	DataSource         string  `json:"data_source"`
	DataPath           string  `json:"data_path"`
	HFDatasetConfig    string  `json:"hf_dataset_config"`
	HFSplit            string  `json:"hf_split"`
	HFMeshBlend        float64 `json:"hf_mesh_blend_fraction"`
	Epochs             int     `json:"epochs"`
	BatchSize          int     `json:"batch_size"`
	LearningRate       float64 `json:"learning_rate"`
	LRTrialMode        bool    `json:"lr_trial_mode"`
	LRTrialEpochs      int     `json:"lr_trial_epochs"`
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
	if (srcRaw == "hf_tabular" || srcRaw == "hybrid_mesh_hf_tabular") && strings.TrimSpace(body.DataPath) == "" {
		jsonErr(w, http.StatusBadRequest, "data_path is required for hf_tabular")
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
		name = "ui_generated_" + strconv.FormatInt(time.Now().Unix(), 10)
	}
	fileName := name + ".toml"
	abs := filepath.Join(genDir, fileName)
	rel := filepath.ToSlash(filepath.Join("configs", "training", fileName))

	content := buildGeneratedTOML(body, accel, src, srcRaw)
	if err := os.WriteFile(abs, []byte(content), 0o644); err != nil {
		jsonErr(w, http.StatusInternalServerError, "failed to write generated config")
		return
	}
	run, err := runManager.start(abs, rel)
	if err != nil {
		jsonErr(w, http.StatusConflict, err.Error())
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusCreated)
	_ = json.NewEncoder(w).Encode(map[string]any{
		"id":     run.ID,
		"config": run.ConfigRel,
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
	if src == "hf_tabular" && strings.TrimSpace(body.DataPath) == "" {
		jsonErr(w, http.StatusBadRequest, "data_path is required for hf_tabular/hybrid")
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
	content := buildGeneratedTOML(body, accel, src, srcRaw)
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
		"ok": true,
		"recommended_lr": parsed["recommended_lr"],
		"promoted":       parsed["promoted"],
		"reason":         parsed["promotion_reason"],
		"metadata_path":  parsed["metadata_path"],
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

func buildGeneratedTOML(body buildRunRequest, accel, src, srcRaw string) string {
	var b strings.Builder
	b.WriteString("# Generated by training-wui Build + Run\n")
	if body.LRTrialMode {
		b.WriteString("# Mode: learning-rate trial run (short budget)\n")
	}
	if srcRaw == "hybrid_mesh_hf_tabular" {
		b.WriteString("# Requested data_source=hybrid_mesh_hf_tabular -> source=hf_tabular + mesh_blend_fraction>0\n")
	}
	b.WriteString("\n")
	b.WriteString("[hardware]\n")
	b.WriteString("accelerator = " + strconv.Quote(accel) + "\n")
	b.WriteString("device_index = 0\n\n")
	b.WriteString("[training]\n")
	b.WriteString("epochs = " + strconv.Itoa(body.Epochs) + "\n")
	b.WriteString("batch_size = " + strconv.Itoa(body.BatchSize) + "\n")
	b.WriteString("learning_rate = " + strconv.FormatFloat(body.LearningRate, 'g', -1, 64) + "\n")
	b.WriteString("grad_clip_norm = 1.0\n\n")
	b.WriteString("[data]\n")
	b.WriteString("source = " + strconv.Quote(src) + "\n")
	if src == "hf_tabular" {
		b.WriteString("path = " + strconv.Quote(strings.TrimSpace(body.DataPath)) + "\n")
	}
	b.WriteString("mesh_algorithms = \"hash,encrypt,network,routing,consensus\"\n\n")
	if src == "hf_tabular" {
		b.WriteString("[huggingface]\n")
		if strings.TrimSpace(body.HFDatasetConfig) != "" {
			b.WriteString("dataset_config = " + strconv.Quote(strings.TrimSpace(body.HFDatasetConfig)) + "\n")
		}
		b.WriteString("split = " + strconv.Quote(strings.TrimSpace(body.HFSplit)) + "\n")
		b.WriteString(
			"mesh_blend_fraction = " + strconv.FormatFloat(body.HFMeshBlend, 'g', -1, 64) + "\n\n",
		)
	}
	if body.LRTrialMode {
		b.WriteString("[eval]\n")
		b.WriteString("holdout_fraction = 0.05\n")
		b.WriteString("every_epoch = true\n\n")
	}
	b.WriteString("[cascade]\n")
	b.WriteString("enabled = true\n")
	b.WriteString("steps_per_epoch = 2\n")
	b.WriteString("group_size = 4\n")
	return b.String()
}

func handlePreflight(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	configRel := strings.TrimSpace(r.URL.Query().Get("config"))
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

from engine.config import EngineConfig
from qminiwasm.hardware.device import get_device
from qminiwasm.hardware.sycl.sycl_hardware import SYCLHardware

cfg_path = sys.argv[1] if len(sys.argv) > 1 else ""
if cfg_path:
    cfg = EngineConfig.from_training_toml(cfg_path)
else:
    cfg = EngineConfig()

requested = cfg.accelerator if cfg.accelerator else (os.environ.get("ACCELERATOR", "").strip() or "auto")
device = get_device(accelerator=cfg.accelerator, device_index=cfg.device_index)
sy = SYCLHardware()
is_active = False
if hasattr(sy, "is_backend_active"):
    try:
        is_active = bool(sy.is_backend_active())
    except Exception:
        is_active = False
else:
    is_active = getattr(sy, "device", None) is not None

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
    "resolved_torch_device": str(device),
    "sycl_backend_active": is_active,
    "dpctl_device_count": dpctl_count,
    "dpctl_error": dpctl_error,
}
print(json.dumps(out))
`
	ctx, cancel := context.WithTimeout(r.Context(), 12*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, pythonExe, "-c", probe, configAbs)
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
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok": true,
		"preflight": parsed,
	})
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
	if len(parts) == 2 && parts[1] == "stop" && r.Method == http.MethodPost {
		handleRunStop(w, r, id)
		return
	}
	http.NotFound(w, r)
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
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"text":         chunk,
		"next_offset":  next,
		"running":      running,
		"exit_code":    runManager.exitCode(id),
	})
}

func handleRunStop(w http.ResponseWriter, r *http.Request, id string) {
	err := runManager.stop(id)
	if err != nil {
		jsonErr(w, http.StatusBadRequest, err.Error())
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": true})
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

type runRecord struct {
	ID         string    `json:"id"`
	ConfigRel  string    `json:"config"`
	Started    time.Time `json:"started"`
	Running    bool      `json:"running"`
	ExitCode   int       `json:"exit_code,omitempty"`
	Error      bool      `json:"error"`
	cmd        *exec.Cmd
	logMu      sync.Mutex
	logBuf     bytes.Buffer
	maxRuns    int // ring of finished ids for list
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

func (m *manager) start(absConfig, relDisplay string) (*runRecord, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	for _, id := range m.ordered {
		if r := m.byID[id]; r != nil && r.Running {
			return nil, fmt.Errorf("a run is already in progress (%s)", id[:8])
		}
	}

	id := newRunID()
	rec := &runRecord{
		ID:        id,
		ConfigRel: relDisplay,
		Started:   time.Now(),
		Running:   true,
	}

	cmd := exec.Command(pythonExe, "-m", "engine", "--config", absConfig)
	cmd.Dir = repoRoot
	cmd.Env = os.Environ()

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
	m.byID[id] = rec
	m.ordered = append([]string{id}, m.ordered...)
	if len(m.ordered) > 32 {
		m.ordered = m.ordered[:32]
	}

	go m.pump(id, stdout)
	go m.pump(id, stderr)
	go m.wait(id)

	return rec, nil
}

func (m *manager) pump(id string, r io.Reader) {
	buf := make([]byte, 4096)
	for {
		n, err := r.Read(buf)
		if n > 0 {
			m.appendLog(id, buf[:n])
		}
		if err != nil {
			return
		}
	}
}

func (m *manager) appendLog(id string, p []byte) {
	m.mu.Lock()
	rec := m.byID[id]
	m.mu.Unlock()
	if rec == nil {
		return
	}
	rec.logMu.Lock()
	rec.logBuf.Write(p)
	rec.logMu.Unlock()
}

func (m *manager) wait(id string) {
	m.mu.Lock()
	rec := m.byID[id]
	cmd := rec.cmd
	m.mu.Unlock()
	if cmd == nil {
		return
	}
	err := cmd.Wait()
	m.mu.Lock()
	rec = m.byID[id]
	if rec != nil {
		rec.Running = false
		if err != nil {
			rec.Error = true
			if x, ok := err.(*exec.ExitError); ok {
				rec.ExitCode = x.ExitCode()
			} else {
				rec.ExitCode = -1
			}
		}
	}
	m.mu.Unlock()
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
		out = append(out, map[string]any{
			"id":         r.ID,
			"config":     r.ConfigRel,
			"started":    r.Started.Format(time.RFC3339),
			"running":    r.Running,
			"exit_code":  r.ExitCode,
			"error":      r.Error,
		})
	}
	return out
}

func (m *manager) stop(id string) error {
	m.mu.Lock()
	rec := m.byID[id]
	if rec == nil || !rec.Running || rec.cmd == nil || rec.cmd.Process == nil {
		m.mu.Unlock()
		return errors.New("run not active")
	}
	proc := rec.cmd.Process
	m.mu.Unlock()
	return proc.Kill()
}
