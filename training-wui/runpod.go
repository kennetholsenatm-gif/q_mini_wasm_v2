// RunPod + OpenTofu helpers: provision/destroy pods under infra/runpod.
// Auth: RUNPOD_API_KEY or RUNPOD_TOKEN (same convention as infra/runpod/tofu.sh).
package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

func runpodInfraDir() string {
	return filepath.Join(repoRoot, "infra", "runpod")
}

func tofuBinary() (string, error) {
	for _, name := range []string{"tofu", "terraform"} {
		if p, err := exec.LookPath(name); err == nil {
			return p, nil
		}
	}
	return "", errors.New("OpenTofu (tofu) or Terraform not found in PATH")
}

// ensureRunpodAPIKey mirrors infra/runpod/tofu.sh: prefer RUNPOD_API_KEY, else map from RUNPOD_TOKEN.
func ensureRunpodAPIKey(env []string) []string {
	hasAPI := false
	hasTok := false
	tokVal := ""
	for _, e := range env {
		if strings.HasPrefix(e, "RUNPOD_API_KEY=") {
			hasAPI = true
		}
		if strings.HasPrefix(e, "RUNPOD_TOKEN=") {
			hasTok = true
			if i := strings.IndexByte(e, '='); i >= 0 {
				tokVal = e[i+1:]
			}
		}
	}
	if !hasAPI && hasTok && strings.TrimSpace(tokVal) != "" {
		return append(env, "RUNPOD_API_KEY="+tokVal)
	}
	return env
}

func runpodTokenPresent() bool {
	if strings.TrimSpace(os.Getenv("RUNPOD_API_KEY")) != "" {
		return true
	}
	return strings.TrimSpace(os.Getenv("RUNPOD_TOKEN")) != ""
}

func runTofu(ctx context.Context, dir string, args ...string) ([]byte, error) {
	bin, err := tofuBinary()
	if err != nil {
		return nil, err
	}
	cmd := exec.CommandContext(ctx, bin, args...)
	cmd.Dir = dir
	cmd.Env = ensureRunpodAPIKey(os.Environ())
	return cmd.CombinedOutput()
}

func runpodNeedsInit(dir string) bool {
	st, err := os.Stat(filepath.Join(dir, ".terraform"))
	return err != nil || !st.IsDir()
}

func runpodEnsureInit(ctx context.Context) error {
	dir := runpodInfraDir()
	if st, err := os.Stat(dir); err != nil || !st.IsDir() {
		return fmt.Errorf("infra/runpod not found under repo root (got %q)", dir)
	}
	if !runpodNeedsInit(dir) {
		return nil
	}
	out, err := runTofu(ctx, dir, "init", "-input=false", "-no-color")
	if err != nil {
		return fmt.Errorf("%v\n%s", err, string(out))
	}
	return nil
}

// runpodApply runs tofu apply -auto-approve and returns combined output text.
func runpodApply(ctx context.Context) (string, error) {
	if !runpodTokenPresent() {
		return "", errors.New("set RUNPOD_TOKEN or RUNPOD_API_KEY (e.g. in repo .env)")
	}
	dir := runpodInfraDir()
	if err := runpodEnsureInit(ctx); err != nil {
		return "", err
	}
	out, err := runTofu(ctx, dir, "apply", "-auto-approve", "-no-color")
	s := string(out)
	if err != nil {
		return s, fmt.Errorf("%v", err)
	}
	return s, nil
}

// runpodDestroy runs tofu destroy -auto-approve.
func runpodDestroy(ctx context.Context) (string, error) {
	if !runpodTokenPresent() {
		return "", errors.New("set RUNPOD_TOKEN or RUNPOD_API_KEY (e.g. in repo .env)")
	}
	dir := runpodInfraDir()
	if _, err := os.Stat(filepath.Join(dir, ".terraform")); err != nil {
		// Nothing to destroy if never initialized.
		return "", nil
	}
	out, err := runTofu(ctx, dir, "destroy", "-auto-approve", "-no-color")
	s := string(out)
	if err != nil {
		return s, fmt.Errorf("%v", err)
	}
	return s, nil
}

func runpodOutputsJSON(ctx context.Context) (map[string]any, error) {
	dir := runpodInfraDir()
	bin, err := tofuBinary()
	if err != nil {
		return nil, err
	}
	cmd := exec.CommandContext(ctx, bin, "output", "-json", "-no-color")
	cmd.Dir = dir
	cmd.Env = ensureRunpodAPIKey(os.Environ())
	out, err := cmd.Output()
	if err != nil {
		return nil, err
	}
	var raw map[string]any
	if err := json.Unmarshal(out, &raw); err != nil {
		return nil, err
	}
	// Flatten terraform output JSON: each key has { "sensitive", "type", "value" }.
	flat := make(map[string]any)
	for k, v := range raw {
		if m, ok := v.(map[string]any); ok {
			if val, ok := m["value"]; ok {
				flat[k] = val
				continue
			}
		}
		flat[k] = v
	}
	return flat, nil
}

func handleRunpodStatus(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	dir := runpodInfraDir()
	_, dirErr := os.Stat(dir)
	bin, binErr := tofuBinary()
	binName := ""
	if binErr == nil {
		binName = filepath.Base(bin)
	}
	ctx, cancel := context.WithTimeout(r.Context(), 12*time.Second)
	defer cancel()

	out := map[string]any{
		"token_present":     runpodTokenPresent(),
		"infra_dir":         filepath.ToSlash(dir),
		"infra_dir_exists":  dirErr == nil,
		"tofu_binary":       binName,
		"tofu_usable":       binErr == nil,
		"tofu_error":        errString(binErr),
		"outputs":           nil,
		"outputs_error":     "",
	}
	if dirErr == nil && binErr == nil && runpodTokenPresent() {
		if o, err := runpodOutputsJSON(ctx); err == nil {
			out["outputs"] = o
		} else {
			out["outputs_error"] = err.Error()
		}
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "runpod": out})
}

func errString(err error) string {
	if err == nil {
		return ""
	}
	return err.Error()
}
