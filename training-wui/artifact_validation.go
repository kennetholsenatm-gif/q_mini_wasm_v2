package main

import (
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingconfig"
)

const tpemInterchangeMagic = "QMWTPEM2"

// ValidateTpemInterchangeFile checks native TPEM interchange v2 layout (magic + JSON envelope + non-empty tail).
func ValidateTpemInterchangeFile(abs string) error {
	raw, err := os.ReadFile(abs)
	if err != nil {
		return err
	}
	if len(raw) < 16 {
		return errors.New("file too small for interchange header")
	}
	if string(raw[:8]) != tpemInterchangeMagic {
		return errors.New("expected QMWTPEM2 magic")
	}
	jsonLen := binary.LittleEndian.Uint64(raw[8:16])
	if jsonLen > uint64(len(raw)-16) {
		return errors.New("invalid JSON length in header")
	}
	envBytes := raw[16 : 16+jsonLen]
	var env struct {
		FormatVersion float64 `json:"format_version"`
	}
	if err := json.Unmarshal(envBytes, &env); err != nil {
		return fmt.Errorf("envelope JSON: %w", err)
	}
	if int(env.FormatVersion) != 2 {
		return fmt.Errorf("unsupported format_version %v (want 2)", env.FormatVersion)
	}
	tail := raw[16+jsonLen:]
	if len(tail) < 16 {
		return errors.New("safetensors blob too small")
	}
	return nil
}

func repoAbs(rel string) string {
	rel = strings.TrimSpace(rel)
	if rel == "" {
		return ""
	}
	return filepath.Join(repoRoot, filepath.FromSlash(rel))
}

// ValidateTrainingRunArtifacts inspects checkpoint paths from training TOML (repo-relative).
func ValidateTrainingRunArtifacts(configAbs string) map[string]any {
	out := map[string]any{
		"ok":     false,
		"checks": []map[string]any{},
	}
	doc, err := trainingconfig.ParseTrainingDocFile(configAbs)
	if err != nil {
		out["error"] = err.Error()
		return out
	}
	_, save, best, latest := trainingconfig.MergeCheckpointPaths(&doc)
	var checks []map[string]any
	okAll := true

	add := func(role, rel string, wantInterchange bool) {
		row := map[string]any{"role": role, "path": rel, "ok": false}
		p := repoAbs(rel)
		if p == "" {
			row["detail"] = "path empty"
			okAll = false
			checks = append(checks, row)
			return
		}
		st, err := os.Stat(p)
		if err != nil {
			row["detail"] = err.Error()
			okAll = false
			checks = append(checks, row)
			return
		}
		if st.Size() < 32 {
			row["detail"] = "file too small"
			okAll = false
			checks = append(checks, row)
			return
		}
		if wantInterchange {
			if err := ValidateTpemInterchangeFile(p); err != nil {
				row["detail"] = err.Error()
				okAll = false
				checks = append(checks, row)
				return
			}
			row["detail"] = "interchange v2"
		} else {
			row["detail"] = "present"
		}
		row["ok"] = true
		checks = append(checks, row)
	}

	add("checkpoint.save_path", save, true)
	add("checkpoint.best_path", best, true)
	add("checkpoint.latest_path", latest, true)

	teacher := strings.TrimSpace(doc.CascadeCurriculumLoop.TeacherCheckpointPath)
	if teacher != "" {
		add("cascade.teacher_checkpoint_path", teacher, true)
	}

	out["checks"] = checks
	out["ok"] = okAll
	if !okAll {
		out["summary"] = "one or more artifacts missing or invalid"
	} else {
		out["summary"] = "TPEM interchange OK for listed paths"
	}
	return out
}

const artifactValidationCacheTTL = 15 * time.Second

// artifactValidationForList returns cached native artifact checks for finished runs (refreshed every TTL).
func (r *runRecord) artifactValidationForList() map[string]any {
	if r == nil || r.Running {
		return nil
	}
	r.artifactValMu.Lock()
	defer r.artifactValMu.Unlock()
	if r.artifactVal != nil && !r.artifactValAt.IsZero() && time.Since(r.artifactValAt) < artifactValidationCacheTTL {
		return r.artifactVal
	}
	cfg := strings.TrimSpace(r.ConfigRel)
	if cfg == "" {
		r.artifactVal = map[string]any{"skipped": true, "reason": "no config path"}
		r.artifactValAt = time.Now()
		return r.artifactVal
	}
	abs := filepath.Join(repoRoot, filepath.FromSlash(cfg))
	if _, err := os.Stat(abs); err != nil {
		r.artifactVal = map[string]any{"skipped": true, "reason": "config file not found", "detail": err.Error()}
		r.artifactValAt = time.Now()
		return r.artifactVal
	}
	r.artifactVal = ValidateTrainingRunArtifacts(abs)
	r.artifactValAt = time.Now()
	return r.artifactVal
}

func handleRunArtifactsValidate(w http.ResponseWriter, r *http.Request, id string) {
	if !authOK(r) {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	runManager.mu.Lock()
	rec := runManager.byID[id]
	runManager.mu.Unlock()
	if rec == nil {
		jsonErr(w, http.StatusNotFound, "run not found")
		return
	}
	cfg := strings.TrimSpace(rec.ConfigRel)
	if cfg == "" {
		jsonErr(w, http.StatusBadRequest, "run has no config path")
		return
	}
	abs := filepath.Join(repoRoot, filepath.FromSlash(cfg))
	if _, err := os.Stat(abs); err != nil {
		jsonErr(w, http.StatusNotFound, "config file not found: "+err.Error())
		return
	}
	val := ValidateTrainingRunArtifacts(abs)
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(val)
}
