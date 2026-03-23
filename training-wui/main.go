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

	var addrFromFlag, pyFromFlag bool
	args := os.Args[1:]
	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "-addr":
			if i+1 < len(args) {
				i++
				addr = args[i]
				addrFromFlag = true
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
				pyFromFlag = true
			}
		}
	}

	abs, err := filepath.Abs(root)
	if err != nil {
		log.Fatal(err)
	}
	repoRoot = filepath.Clean(abs)

	// Load repo .env (e.g. /opt/qmw/.env from bind mount) so IBM/HF tokens and
	// TRAINING_WUI_* are visible to this process and subprocesses (python -m engine).
	loadDotenvFromRepo(repoRoot)

	if !addrFromFlag {
		if v := os.Getenv("TRAINING_WUI_ADDR"); v != "" {
			addr = v
		}
	}
	if !pyFromFlag {
		if v := os.Getenv("TRAINING_WUI_PYTHON"); v != "" {
			py = v
		}
	}
	pythonExe = py

	trainingDir := filepath.Join(repoRoot, "configs", "training")
	if st, err := os.Stat(trainingDir); err != nil || !st.IsDir() {
		log.Printf("warning: %q missing or not a directory (set -root to repo root)", trainingDir)
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/api/configs", handleConfigs)
	mux.HandleFunc("/api/preflight", handlePreflight)
	mux.HandleFunc("/api/model/facts", handleModelFacts)
	mux.HandleFunc("/api/lr/auto", handleAutoLR)
	mux.HandleFunc("/api/runs", handleRunsCollection)
	mux.HandleFunc("/api/runs/build", handleRunBuild)
	mux.HandleFunc("/api/runpod/status", handleRunpodStatus)
	mux.HandleFunc("/api/runpod/tofu", handleRunpodTofu)
	mux.HandleFunc("/api/runpod/tfvars", handleRunpodTfvars)
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
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS")
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
			Config                string `json:"config"`
			QuantumBackend        string `json:"quantum_backend"`
			QuantumPolicy         string `json:"quantum_policy"`
			IBMBackendName        string `json:"ibm_backend_name"`
			RunTarget             string `json:"run_target"`
			RunpodDestroyOnExit   *bool  `json:"runpod_destroy_on_exit"`
			RunpodVarFile         string `json:"runpod_var_file"`
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
		if missing, err := missingLoadCheckpointFromConfig(abs); err != nil {
			jsonErr(w, http.StatusBadRequest, "invalid checkpoint config: "+err.Error())
			return
		} else if missing != "" {
			jsonErr(w, http.StatusBadRequest, "checkpoint load_path not found: "+missing)
			return
		}
		extraEnv := buildQuantumEnvOverrides(body.QuantumBackend, body.QuantumPolicy, body.IBMBackendName)
		opts := runStartOptsFromRequest(body.RunTarget, body.RunpodDestroyOnExit, body.RunpodVarFile)
		if err := validateRunTarget(opts); err != nil {
			jsonErr(w, http.StatusBadRequest, err.Error())
			return
		}
		run, err := runManager.start(abs, body.Config, extraEnv, opts)
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

func PathJoinRepo(root, maybeRel string) string {
	p := filepath.Clean(maybeRel)
	if filepath.IsAbs(p) {
		return p
	}
	return filepath.Join(root, filepath.FromSlash(p))
}

type runStartOpts struct {
	RunTarget             string `json:"run_target"`
	RunpodDestroyOnExit   bool   `json:"runpod_destroy_on_exit"`
	RunpodVarFile         string `json:"runpod_var_file"` // optional basename under infra/runpod, e.g. terraform.tfvars
}

func normalizeRunTarget(s string) string {
	s = strings.ToLower(strings.TrimSpace(s))
	if s == "" {
		return "local"
	}
	return s
}

func runStartOptsFromRequest(runTarget string, destroyPtr *bool, runpodVarFile string) runStartOpts {
	rt := normalizeRunTarget(runTarget)
	destroy := true
	if destroyPtr != nil {
		destroy = *destroyPtr
	}
	if rt != "runpod" {
		destroy = false
	}
	return runStartOpts{RunTarget: rt, RunpodDestroyOnExit: destroy, RunpodVarFile: strings.TrimSpace(runpodVarFile)}
}

func validateRunTarget(o runStartOpts) error {
	switch o.RunTarget {
	case "local", "runpod":
		return nil
	default:
		return errors.New("run_target must be local or runpod")
	}
}

type buildRunRequest struct {
	Name               string  `json:"name"`
	Accelerator        string  `json:"accelerator"`
	QuantumBackend     string  `json:"quantum_backend"`
	QuantumPolicy      string  `json:"quantum_policy"`
	IBMBackendName     string  `json:"ibm_backend_name"`
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
	ResumeLatest       bool    `json:"resume_latest"`
	RunTarget          string  `json:"run_target"`
	RunpodDestroyOnExit *bool  `json:"runpod_destroy_on_exit"`
	RunpodVarFile      string  `json:"runpod_var_file"`
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
		name = "model_" + strconv.FormatInt(time.Now().Unix(), 10)
	}
	fileName := name + ".toml"
	abs := filepath.Join(genDir, fileName)
	rel := filepath.ToSlash(filepath.Join("configs", "training", fileName))
	latestRel := filepath.ToSlash(filepath.Join("artifacts", name+"_latest.pt"))
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
	extraEnv := buildQuantumEnvOverrides(body.QuantumBackend, body.QuantumPolicy, body.IBMBackendName)
	opts := runStartOptsFromRequest(body.RunTarget, body.RunpodDestroyOnExit, body.RunpodVarFile)
	if err := validateRunTarget(opts); err != nil {
		jsonErr(w, http.StatusBadRequest, err.Error())
		return
	}
	run, err := runManager.start(abs, rel, extraEnv, opts)
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
from pathlib import Path
from engine.config import EngineConfig
from qminiwasm.model import QMiniWASM

cfg_path = os.environ["QMW_FACTS_CONFIG"]
model_stem = os.environ["QMW_FACTS_STEM"]
repo_root = Path(os.environ["QMW_FACTS_REPO"]).resolve()
cfg = EngineConfig.from_training_toml(cfg_path)
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
)

def count_params(obj):
    if obj is None:
        return 0
    return int(sum(p.numel() for p in obj.parameters()))

parts = {
    "quantum_router": count_params(m.quantum_router),
    "ternary_expert": count_params(m.ternary_expert),
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

latest_rel = f"artifacts/{model_stem}_latest.pt"
latest_abs = repo_root / latest_rel
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
    "hf_token_present": bool(getattr(cfg, "hf_token", None)),
  },
  "artifacts": {
    "save_path": f"artifacts/{model_stem}.pt",
    "best_path": f"artifacts/{model_stem}_best.pt",
    "latest_path": latest_rel,
    "resume_available": latest_abs.is_file(),
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
	env = append(env, "QMINIWASM_FORCE_MOCK_WASM=1")
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
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "facts": parsed})
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
	if qbe := effectiveQuantumBackendForPolicy(body.QuantumBackend, body.QuantumPolicy, body.IBMBackendName); qbe != "" {
		b.WriteString("quantum_backend = " + strconv.Quote(qbe) + "\n")
	}
	b.WriteString("device_index = 0\n\n")
	b.WriteString("[training]\n")
	b.WriteString("epochs = " + strconv.Itoa(body.Epochs) + "\n")
	b.WriteString("batch_size = " + strconv.Itoa(body.BatchSize) + "\n")
	b.WriteString("learning_rate = " + strconv.FormatFloat(body.LearningRate, 'g', -1, 64) + "\n")
	b.WriteString("grad_clip_norm = 1.0\n\n")
	b.WriteString("[checkpoint]\n")
	b.WriteString("save_path = " + strconv.Quote(filepath.ToSlash(filepath.Join("artifacts", modelStem+".pt"))) + "\n")
	b.WriteString("best_path = " + strconv.Quote(filepath.ToSlash(filepath.Join("artifacts", modelStem+"_best.pt"))) + "\n")
	b.WriteString("latest_path = " + strconv.Quote(filepath.ToSlash(filepath.Join("artifacts", modelStem+"_latest.pt"))) + "\n\n")
	if body.ResumeLatest {
		b.WriteString("load_path = " + strconv.Quote(filepath.ToSlash(filepath.Join("artifacts", modelStem+"_latest.pt"))) + "\n\n")
	}
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

from engine.config import EngineConfig
from engine._dotenv import load_dotenv_if_available
from qminiwasm.hardware.device import get_device
from qminiwasm.hardware.sycl.sycl_hardware import SYCLHardware

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
    "accelerator_from_config_file": (cfg.accelerator or ""),
    "preflight_accelerator_override": preflight_accel,
    "resolved_torch_device": str(device),
    "sycl_backend_active": is_active,
    "dpctl_device_count": dpctl_count,
    "dpctl_error": dpctl_error,
    "quantum_backend": (os.environ.get("QUANTUM_BACKEND", "").strip() or cfg.quantum_backend or "penny_lane"),
    "quantum_backend_from_config": (cfg.quantum_backend or ""),
    "quantum_policy": (os.environ.get("QUANTUM_EXECUTION_POLICY", "").strip() or "prefer_hardware_fallback"),
}

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
	ID                  string    `json:"id"`
	ConfigRel           string    `json:"config"`
	Started             time.Time `json:"started"`
	Running             bool      `json:"running"`
	ExitCode            int       `json:"exit_code,omitempty"`
	Error               bool      `json:"error"`
	RunTarget           string    `json:"run_target,omitempty"`
	RunpodDestroyOnExit bool      `json:"runpod_destroy_on_exit,omitempty"`
	RunpodVarFile       string    `json:"runpod_var_file,omitempty"`
	cmd                 *exec.Cmd
	logMu               sync.Mutex
	logBuf              bytes.Buffer
	maxRuns             int // ring of finished ids for list
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

func (m *manager) start(absConfig, relDisplay string, extraEnv []string, opts runStartOpts) (*runRecord, error) {
	opts.RunTarget = normalizeRunTarget(opts.RunTarget)
	if err := validateRunTarget(opts); err != nil {
		return nil, err
	}

	m.mu.Lock()
	for _, id := range m.ordered {
		if r := m.byID[id]; r != nil && r.Running {
			m.mu.Unlock()
			return nil, fmt.Errorf("a run is already in progress (%s)", id[:8])
		}
	}
	m.mu.Unlock()

	var applyLog string
	applySucceeded := false
	if opts.RunTarget == "runpod" {
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
	rec := &runRecord{
		ID:                  id,
		ConfigRel:           relDisplay,
		Started:             time.Now(),
		Running:             true,
		RunTarget:           opts.RunTarget,
		RunpodDestroyOnExit: opts.RunpodDestroyOnExit,
		RunpodVarFile:       opts.RunpodVarFile,
	}
	if applyLog != "" {
		rec.logBuf.WriteString("=== runpod: OpenTofu apply ===\n")
		rec.logBuf.WriteString(applyLog)
		rec.logBuf.WriteString("\n=== training: python -m engine ===\n")
	}

	cmd := exec.Command(pythonExe, "-m", "engine", "--config", absConfig)
	cmd.Dir = repoRoot
	cmd.Env = append(os.Environ(), extraEnv...)

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

	registered = true
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
	if rec == nil {
		m.mu.Unlock()
		return
	}
	cmd := rec.cmd
	runTarget := rec.RunTarget
	destroyPod := rec.RunpodDestroyOnExit
	runpodVF := rec.RunpodVarFile
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

	if runTarget == "runpod" && destroyPod {
		ctx, cancel := context.WithTimeout(context.Background(), 20*time.Minute)
		defer cancel()
		out, derr := runpodDestroy(ctx, runpodVF)
		m.appendLog(id, []byte("\n=== runpod: OpenTofu destroy ===\n"))
		m.appendLog(id, []byte(out))
		if derr != nil {
			m.appendLog(id, []byte("\n=== runpod destroy error: "+derr.Error()+" ===\n"))
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
		out = append(out, map[string]any{
			"id":                      r.ID,
			"config":                  r.ConfigRel,
			"started":                 r.Started.Format(time.RFC3339),
			"running":                 r.Running,
			"exit_code":               r.ExitCode,
			"error":                   r.Error,
			"run_target":              r.RunTarget,
			"runpod_destroy_on_exit":  r.RunpodDestroyOnExit,
			"runpod_var_file":         r.RunpodVarFile,
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
