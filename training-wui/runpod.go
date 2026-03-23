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
	"regexp"
	"strings"
	"time"
)

var runpodVarFilePattern = regexp.MustCompile(`^[a-zA-Z0-9][a-zA-Z0-9_.-]*$`)

// sanitizeRunpodVarFile returns a basename under infra/runpod, or "" if unset.
func sanitizeRunpodVarFile(name string) (string, error) {
	name = strings.TrimSpace(name)
	if name == "" {
		return "", nil
	}
	if filepath.Base(name) != name {
		return "", errors.New("var_file must be a filename only (no paths), e.g. terraform.tfvars")
	}
	if !runpodVarFilePattern.MatchString(name) {
		return "", errors.New("var_file: use only letters, digits, and _ . - (e.g. terraform.tfvars)")
	}
	return name, nil
}

// runpodVarFileArgs returns -var-file=ABS if varFile is set and the file exists under dir.
func runpodVarFileArgs(dir, varFile string) ([]string, error) {
	vf, err := sanitizeRunpodVarFile(varFile)
	if err != nil {
		return nil, err
	}
	if vf == "" {
		return nil, nil
	}
	full := filepath.Join(dir, vf)
	if _, err := os.Stat(full); err != nil {
		return nil, fmt.Errorf("var file %q not found under %s — copy terraform.tfvars.example to %s and edit GPU settings", vf, filepath.ToSlash(dir), vf)
	}
	return []string{"-var-file=" + full}, nil
}

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

// runpodTofuInit runs tofu init (always; use from UI for re-init / provider updates).
func runpodTofuInit(ctx context.Context) (string, error) {
	dir := runpodInfraDir()
	if st, err := os.Stat(dir); err != nil || !st.IsDir() {
		return "", fmt.Errorf("infra/runpod not found under repo root (got %q)", dir)
	}
	out, err := runTofu(ctx, dir, "init", "-input=false", "-no-color")
	return string(out), err
}

// runpodPlan runs tofu plan with optional -var-file.
func runpodPlan(ctx context.Context, varFile string) (string, error) {
	if !runpodTokenPresent() {
		return "", errors.New("set RUNPOD_TOKEN or RUNPOD_API_KEY (e.g. in repo .env)")
	}
	dir := runpodInfraDir()
	if err := runpodEnsureInit(ctx); err != nil {
		return "", err
	}
	vfArgs, err := runpodVarFileArgs(dir, varFile)
	if err != nil {
		return "", err
	}
	args := []string{"plan", "-input=false", "-no-color"}
	args = append(args, vfArgs...)
	out, err := runTofu(ctx, dir, args...)
	s := string(out)
	if err != nil {
		return s, fmt.Errorf("%w", err)
	}
	return s, nil
}

// runpodApply runs tofu apply -auto-approve; varFile is optional basename under infra/runpod.
func runpodApply(ctx context.Context, varFile string) (string, error) {
	if !runpodTokenPresent() {
		return "", errors.New("set RUNPOD_TOKEN or RUNPOD_API_KEY (e.g. in repo .env)")
	}
	dir := runpodInfraDir()
	if err := runpodEnsureInit(ctx); err != nil {
		return "", err
	}
	vfArgs, err := runpodVarFileArgs(dir, varFile)
	if err != nil {
		return "", err
	}
	args := []string{"apply"}
	args = append(args, vfArgs...)
	args = append(args, "-auto-approve", "-no-color")
	out, err := runTofu(ctx, dir, args...)
	s := string(out)
	if err != nil {
		return s, fmt.Errorf("%w", err)
	}
	return s, nil
}

// runpodDestroy runs tofu destroy -auto-approve; pass same varFile as apply when using tfvars.
func runpodDestroy(ctx context.Context, varFile string) (string, error) {
	if !runpodTokenPresent() {
		return "", errors.New("set RUNPOD_TOKEN or RUNPOD_API_KEY (e.g. in repo .env)")
	}
	dir := runpodInfraDir()
	if _, err := os.Stat(filepath.Join(dir, ".terraform")); err != nil {
		return "", nil
	}
	vfArgs, err := runpodVarFileArgs(dir, varFile)
	if err != nil {
		return "", err
	}
	args := []string{"destroy"}
	args = append(args, vfArgs...)
	args = append(args, "-auto-approve", "-no-color")
	out, err := runTofu(ctx, dir, args...)
	s := string(out)
	if err != nil {
		return s, fmt.Errorf("%w", err)
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

const maxRunpodTfvarsBytes = 256 * 1024

func runpodTfvarsPath() string {
	return filepath.Join(runpodInfraDir(), "terraform.tfvars")
}

// handleRunpodTfvars reads or writes infra/runpod/terraform.tfvars only.
// GET ?source=example returns terraform.tfvars.example content for the editor.
func handleRunpodTfvars(w http.ResponseWriter, r *http.Request) {
	dir := runpodInfraDir()
	tfPath := runpodTfvarsPath()
	exPath := filepath.Join(dir, "terraform.tfvars.example")

	switch r.Method {
	case http.MethodGet:
		if r.URL.Query().Get("source") == "example" {
			b, err := os.ReadFile(exPath)
			if err != nil {
				jsonErr(w, http.StatusNotFound, "terraform.tfvars.example not found under infra/runpod")
				return
			}
			w.Header().Set("Content-Type", "application/json")
			_ = json.NewEncoder(w).Encode(map[string]any{
				"ok":      true,
				"content": string(b),
				"source":  "example",
			})
			return
		}
		if st, err := os.Stat(dir); err != nil || !st.IsDir() {
			jsonErr(w, http.StatusNotFound, "infra/runpod not found in repo")
			return
		}
		b, err := os.ReadFile(tfPath)
		exists := err == nil
		content := ""
		if exists {
			content = string(b)
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok":      true,
			"content": content,
			"exists":  exists,
			"path":    filepath.ToSlash(filepath.Join("infra", "runpod", "terraform.tfvars")),
		})
	case http.MethodPut:
		if st, err := os.Stat(dir); err != nil || !st.IsDir() {
			jsonErr(w, http.StatusNotFound, "infra/runpod not found in repo")
			return
		}
		var body struct {
			Content string `json:"content"`
		}
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			jsonErr(w, http.StatusBadRequest, "invalid JSON")
			return
		}
		if len(body.Content) > maxRunpodTfvarsBytes {
			jsonErr(w, http.StatusRequestEntityTooLarge, "content too large (max 256 KiB)")
			return
		}
		tmp := tfPath + ".tmp"
		if err := os.WriteFile(tmp, []byte(body.Content), 0o644); err != nil {
			jsonErr(w, http.StatusInternalServerError, err.Error())
			return
		}
		if err := os.Rename(tmp, tfPath); err != nil {
			_ = os.Remove(tmp)
			jsonErr(w, http.StatusInternalServerError, err.Error())
			return
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok":   true,
			"path": filepath.ToSlash(filepath.Join("infra", "runpod", "terraform.tfvars")),
		})
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func handleRunpodTofu(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var body struct {
		Action  string `json:"action"`
		VarFile string `json:"var_file"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		jsonErr(w, http.StatusBadRequest, "invalid JSON")
		return
	}
	action := strings.ToLower(strings.TrimSpace(body.Action))
	var out string
	var err error
	switch action {
	case "init":
		ctx, cancel := context.WithTimeout(r.Context(), 15*time.Minute)
		defer cancel()
		out, err = runpodTofuInit(ctx)
	case "plan":
		ctx, cancel := context.WithTimeout(r.Context(), 20*time.Minute)
		defer cancel()
		out, err = runpodPlan(ctx, body.VarFile)
	case "apply":
		ctx, cancel := context.WithTimeout(r.Context(), 45*time.Minute)
		defer cancel()
		out, err = runpodApply(ctx, body.VarFile)
	case "destroy":
		ctx, cancel := context.WithTimeout(r.Context(), 30*time.Minute)
		defer cancel()
		out, err = runpodDestroy(ctx, body.VarFile)
	default:
		jsonErr(w, http.StatusBadRequest, "action must be init, plan, apply, or destroy")
		return
	}
	w.Header().Set("Content-Type", "application/json")
	if err != nil {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok":     false,
			"error":  err.Error(),
			"output": out,
		})
		return
	}
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "output": out})
}
