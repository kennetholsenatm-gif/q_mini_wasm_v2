// RunPod Serverless (queue-based endpoints): HTTP API client and WUI handlers.
// API: https://docs.runpod.io/serverless/endpoints/send-requests
package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"strings"
	"time"
)

const runpodServerlessAPIBase = "https://api.runpod.ai/v2"

// Default Docker image when the training TOML has no [runpod_serverless] worker_image.
// RunPod publishes this stack on Docker Hub; override per-repo in configs/training/*.toml if needed.
// See https://docs.runpod.io/serverless/workers/deploy
const runpodServerlessBuiltinWorkerImage = "docker.io/your-org/qmw-serverless-alma10:latest"

func runpodServerlessEndpointID() string {
	return strings.TrimSpace(os.Getenv("RUNPOD_SERVERLESS_ENDPOINT_ID"))
}

// runpodServerlessDefaultWorkerImage is the built-in default shown in /api/meta (per-config image comes from TOML or this).
func runpodServerlessDefaultWorkerImage() string {
	return runpodServerlessBuiltinWorkerImage
}

// runpodServerlessDefaultContainerDiskGB is container disk for Serverless templates created from the wizard.
func runpodServerlessDefaultContainerDiskGB() int {
	return 50
}

// runpodAccountAPIKeyForREST is the account-wide key for https://rest.runpod.io (management).
// RUNPOD_TOKEN_END is endpoint-scoped and must not be used there.
func runpodAccountAPIKeyForREST() string {
	if k := strings.TrimSpace(os.Getenv("RUNPOD_API_KEY")); k != "" {
		return k
	}
	return strings.TrimSpace(os.Getenv("RUNPOD_TOKEN"))
}

// runpodServerlessQueueAPIKey is the secret for https://api.runpod.ai/v2/{endpoint}/run|status|…
// Prefer RUNPOD_TOKEN_END (endpoint API key from the RunPod console) when set.
func runpodServerlessQueueAPIKey() string {
	if k := strings.TrimSpace(os.Getenv("RUNPOD_TOKEN_END")); k != "" {
		return k
	}
	return runpodAccountAPIKeyForREST()
}

func runpodServerlessConfigured() bool {
	return runpodServerlessEndpointID() != "" && runpodServerlessQueueAPIKey() != ""
}

func runpodServerlessURL(endpointID, suffix string) string {
	eid := strings.Trim(strings.TrimSpace(endpointID), "/")
	suf := strings.TrimPrefix(strings.TrimSpace(suffix), "/")
	return fmt.Sprintf("%s/%s/%s", runpodServerlessAPIBase, eid, suf)
}

func runpodServerlessDoJSON(ctx context.Context, method, url string, body any) (map[string]any, int, []byte, error) {
	key := runpodServerlessQueueAPIKey()
	if key == "" {
		return nil, 0, nil, errors.New("set RUNPOD_TOKEN_END and/or RUNPOD_API_KEY or RUNPOD_TOKEN for the queue API")
	}
	var rdr io.Reader
	if body != nil {
		b, err := json.Marshal(body)
		if err != nil {
			return nil, 0, nil, err
		}
		rdr = bytes.NewReader(b)
	}
	req, err := http.NewRequestWithContext(ctx, method, url, rdr)
	if err != nil {
		return nil, 0, nil, err
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	req.Header.Set("Accept", "application/json")
	// RunPod queue API: use API key as Authorization value (see operation-reference cURL examples).
	req.Header.Set("Authorization", key)

	client := &http.Client{Timeout: 120 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return nil, 0, nil, err
	}
	defer resp.Body.Close()
	raw, readErr := io.ReadAll(io.LimitReader(resp.Body, 8<<20))
	if readErr != nil {
		return nil, resp.StatusCode, nil, fmt.Errorf("read runpod response: %w", readErr)
	}
	var out map[string]any
	if len(bytes.TrimSpace(raw)) > 0 {
		if err := json.Unmarshal(raw, &out); err != nil {
			const maxPreview = 600
			preview := string(raw)
			if len(preview) > maxPreview {
				preview = preview[:maxPreview] + "...(truncated)"
			}
			return nil, resp.StatusCode, raw, fmt.Errorf("decode runpod response JSON (HTTP %d): %w: %s", resp.StatusCode, err, preview)
		}
	}
	if out == nil {
		out = map[string]any{}
	}
	return out, resp.StatusCode, raw, nil
}

// RunpodServerlessHealth calls GET .../health.
func RunpodServerlessHealth(ctx context.Context, endpointID string) (map[string]any, int, error) {
	url := runpodServerlessURL(endpointID, "health")
	out, code, _, err := runpodServerlessDoJSON(ctx, http.MethodGet, url, nil)
	return out, code, err
}

// RunpodServerlessRunAsync POST .../run
func RunpodServerlessRunAsync(ctx context.Context, endpointID string, payload map[string]any) (map[string]any, int, []byte, error) {
	url := runpodServerlessURL(endpointID, "run")
	return runpodServerlessDoJSON(ctx, http.MethodPost, url, payload)
}

// RunpodServerlessRunSync POST .../runsync — optional ?wait= milliseconds (1000–300000).
func RunpodServerlessRunSync(ctx context.Context, endpointID string, waitMs int, payload map[string]any) (map[string]any, int, []byte, error) {
	url := runpodServerlessURL(endpointID, "runsync")
	if waitMs >= 1000 && waitMs <= 300000 {
		url = fmt.Sprintf("%s?wait=%d", url, waitMs)
	}
	return runpodServerlessDoJSON(ctx, http.MethodPost, url, payload)
}

// RunpodServerlessStatus GET .../status/{jobID}
func RunpodServerlessStatus(ctx context.Context, endpointID, jobID string) (map[string]any, int, []byte, error) {
	jobID = strings.TrimSpace(jobID)
	if jobID == "" {
		return nil, 0, nil, errors.New("job id is empty")
	}
	url := runpodServerlessURL(endpointID, "status/"+jobID)
	return runpodServerlessDoJSON(ctx, http.MethodGet, url, nil)
}

// RunpodServerlessCancel POST .../cancel/{jobID}
func RunpodServerlessCancel(ctx context.Context, endpointID, jobID string) error {
	jobID = strings.TrimSpace(jobID)
	if jobID == "" {
		return errors.New("job id is empty")
	}
	url := runpodServerlessURL(endpointID, "cancel/"+jobID)
	out, code, raw, err := runpodServerlessDoJSON(ctx, http.MethodPost, url, nil)
	if err != nil {
		return err
	}
	if code >= 400 {
		return fmt.Errorf("cancel failed: HTTP %d: %s", code, string(raw))
	}
	if s, _ := out["status"].(string); strings.EqualFold(s, "CANCELLED") {
		return nil
	}
	if code >= 200 && code < 300 {
		return nil
	}
	return fmt.Errorf("cancel: unexpected response %d: %s", code, string(raw))
}

func handleRunpodServerlessMeta(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	eid := runpodServerlessEndpointID()
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok":                   true,
		"endpoint_configured":  eid != "",
		"token_present":        runpodServerlessQueueAPIKey() != "",
		"endpoint_key_present": strings.TrimSpace(os.Getenv("RUNPOD_TOKEN_END")) != "",
		"api_base":             runpodServerlessAPIBase,
		"docs_overview":        "https://docs.runpod.io/serverless/overview",
		"docs_send_requests":   "https://docs.runpod.io/serverless/endpoints/send-requests",
		"docs_operation_ref":   "https://docs.runpod.io/serverless/endpoints/operation-reference",
		"docs_handler":         "https://docs.runpod.io/serverless/workers/handler-functions",
		"wui_train_input_docs": "docs/RUNPOD_SERVERLESS.md (qmw_schema_version / train job input)",
	})
}

func handleRunpodServerlessRun(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	eid := runpodServerlessEndpointID()
	if eid == "" {
		jsonErr(w, http.StatusBadRequest, "set RUNPOD_SERVERLESS_ENDPOINT_ID in repo .env and restart training-wui")
		return
	}
	if runpodServerlessQueueAPIKey() == "" {
		jsonErr(w, http.StatusBadRequest, "set RUNPOD_TOKEN_END (endpoint key) and/or RUNPOD_API_KEY or RUNPOD_TOKEN in .env")
		return
	}
	var body map[string]any
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonErr(w, http.StatusBadRequest, "invalid JSON")
		return
	}
	source := strings.TrimSpace(r.Header.Get("X-QMW-Source"))
	mode, _ := body["mode"].(string)
	delete(body, "mode")
	waitVal := 120000
	if wv, ok := body["wait_ms"].(float64); ok {
		waitVal = int(wv)
	}
	delete(body, "wait_ms")
	if _, has := body["input"]; !has {
		jsonErr(w, http.StatusBadRequest, "body must include an \"input\" object (see RunPod send-requests docs)")
		return
	}
	if in, ok := body["input"].(map[string]any); ok {
		action := strings.TrimSpace(fmt.Sprint(in["qmw_action"]))
		cfgRel := strings.TrimSpace(fmt.Sprint(in["config_rel"]))
		log.Printf("runpod_serverless/run source=%q mode=%q qmw_action=%q config_rel=%q", source, strings.TrimSpace(mode), action, cfgRel)
	} else {
		log.Printf("runpod_serverless/run source=%q mode=%q input_type=%T", source, strings.TrimSpace(mode), body["input"])
	}
	ctx, cancel := context.WithTimeout(r.Context(), 15*time.Minute)
	defer cancel()
	var out map[string]any
	var code int
	var raw []byte
	var err error
	if strings.EqualFold(strings.TrimSpace(mode), "sync") {
		out, code, raw, err = RunpodServerlessRunSync(ctx, eid, waitVal, body)
	} else {
		out, code, raw, err = RunpodServerlessRunAsync(ctx, eid, body)
	}
	if err != nil {
		jsonErr(w, http.StatusBadGateway, err.Error())
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	if code >= 400 {
		w.WriteHeader(code)
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok":     false,
			"status": code,
			"runpod": out,
			"raw":    string(raw),
			"error":  fmt.Sprintf("RunPod API HTTP %d", code),
		})
		return
	}
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok":             true,
		"status":         code,
		"runpod":         out,
		"request_source": source,
	})
}

func handleRunpodServerlessJob(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	eid := runpodServerlessEndpointID()
	if eid == "" {
		jsonErr(w, http.StatusBadRequest, "RUNPOD_SERVERLESS_ENDPOINT_ID not set")
		return
	}
	if runpodServerlessQueueAPIKey() == "" {
		jsonErr(w, http.StatusBadRequest, "set RUNPOD_TOKEN_END and/or RUNPOD_API_KEY or RUNPOD_TOKEN in .env")
		return
	}
	jobID := strings.TrimSpace(r.URL.Query().Get("id"))
	if jobID == "" {
		jsonErr(w, http.StatusBadRequest, "query id=JOB_ID is required")
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 60*time.Second)
	defer cancel()
	out, code, raw, err := RunpodServerlessStatus(ctx, eid, jobID)
	if err != nil {
		jsonErr(w, http.StatusBadGateway, err.Error())
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	if code >= 400 {
		w.WriteHeader(code)
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok":     false,
			"status": code,
			"runpod": out,
			"raw":    string(raw),
		})
		return
	}
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "runpod": out})
}

func handleRunpodServerlessHealth(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	eid := runpodServerlessEndpointID()
	if eid == "" {
		jsonErr(w, http.StatusBadRequest, "RUNPOD_SERVERLESS_ENDPOINT_ID not set")
		return
	}
	if runpodServerlessQueueAPIKey() == "" {
		jsonErr(w, http.StatusBadRequest, "set RUNPOD_TOKEN_END and/or RUNPOD_API_KEY or RUNPOD_TOKEN in .env")
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()
	out, code, err := RunpodServerlessHealth(ctx, eid)
	if err != nil {
		jsonErr(w, http.StatusBadGateway, err.Error())
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	if code >= 400 {
		w.WriteHeader(code)
		_ = json.NewEncoder(w).Encode(map[string]any{"ok": false, "status": code, "runpod": out})
		return
	}
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "runpod": out})
}

// buildQMWServerlessTrainInputV1 is the WUI → worker contract for Launch with run_target runpod_serverless.
// Workers should read input["qmw_action"] == "train" and input["config_rel"] (repo-relative path).
// Optional input.toml_overlay is a TOML fragment deep-merged into the base config on the worker (non-secret).
func buildQMWServerlessTrainInputV1(configRel string, extraEnv []string, tomlOverlay string) map[string]any {
	extraEnv = appendRuntimeEnvDefaults(extraEnv)
	in := map[string]any{
		"qmw_schema_version": 1,
		"qmw_action":         "train",
		"config_rel":         configRel,
		"extra_env":          extraEnv,
	}
	if strings.TrimSpace(tomlOverlay) != "" {
		in["toml_overlay"] = tomlOverlay
	}
	return map[string]any{
		"input": in,
		"policy": map[string]any{
			"executionTimeout": 172800000, // 48h active (ms)
			"ttl":              259200000, // 72h total (ms)
		},
	}
}

func appendRuntimeEnvDefaults(extraEnv []string) []string {
	existing := map[string]bool{}
	for _, kv := range extraEnv {
		if i := strings.Index(kv, "="); i > 0 {
			existing[strings.TrimSpace(kv[:i])] = true
		}
	}
	for _, k := range []string{
		"QMINIWASM_TRAINING_RUNTIME_MODE",
		"QMINIWASM_TERNARY_IMPL",
		"QMINIWASM_TRIT_PACK_IMPL",
		"QMINIWASM_MEMORY_ENCODE_IMPL",
		"QMINIWASM_WASM_EXEC_IMPL",
		"QMINIWASM_CASCADE_RL_IMPL",
		"QMINIWASM_NATIVE_STRICT",
	} {
		if existing[k] {
			continue
		}
		if v := strings.TrimSpace(os.Getenv(k)); v != "" {
			extraEnv = append(extraEnv, k+"="+v)
		}
	}
	return extraEnv
}
