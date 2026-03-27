// Warm RunPod SSH targets: optional registry so training launch can skip OpenTofu apply.
package main

import (
	"encoding/json"
	"errors"
	"io"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"
)

// warmTarget is one saved remote GPU host (typically RunPod public_ip after tofu apply).
type warmTarget struct {
	ID        string `json:"id"`
	Label     string `json:"label"`
	Host      string `json:"host"`
	SSHUser   string `json:"ssh_user,omitempty"`
	Notes     string `json:"notes,omitempty"`
	UpdatedAt string `json:"updated_at"`
}

type warmTargetsFile struct {
	Targets []warmTarget `json:"targets"`
}

var (
	warmTargetsMu     sync.Mutex
	warmTargetIDRe    = regexp.MustCompile(`^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$`)
	warmTargetHostRe = regexp.MustCompile(`^[a-zA-Z0-9]([a-zA-Z0-9:.\-_]{0,253}[a-zA-Z0-9])?$`)
)

func warmTargetsJSONPath() string {
	return filepath.Join(repoRoot, "configs", "training", ".wui_runpod_targets.json")
}

func loadWarmTargets() (*warmTargetsFile, error) {
	p := warmTargetsJSONPath()
	b, err := os.ReadFile(p)
	if err != nil {
		if os.IsNotExist(err) {
			return &warmTargetsFile{Targets: []warmTarget{}}, nil
		}
		return nil, err
	}
	var f warmTargetsFile
	if err := json.Unmarshal(b, &f); err != nil {
		return nil, err
	}
	if f.Targets == nil {
		f.Targets = []warmTarget{}
	}
	return &f, nil
}

func saveWarmTargets(f *warmTargetsFile) error {
	if f.Targets == nil {
		f.Targets = []warmTarget{}
	}
	dir := filepath.Dir(warmTargetsJSONPath())
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	b, err := json.MarshalIndent(f, "", "  ")
	if err != nil {
		return err
	}
	tmp := warmTargetsJSONPath() + ".tmp"
	if err := os.WriteFile(tmp, b, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, warmTargetsJSONPath())
}

func validateWarmTargetHost(host string) error {
	host = strings.TrimSpace(host)
	if host == "" {
		return errors.New("host is required")
	}
	if strings.Contains(host, "/") || strings.Contains(host, "..") {
		return errors.New("invalid host")
	}
	if ip := net.ParseIP(strings.Trim(host, "[]")); ip != nil {
		return nil
	}
	if !warmTargetHostRe.MatchString(host) {
		return errors.New("host must be an IP or DNS hostname")
	}
	return nil
}

func validateWarmTargetEntry(t *warmTarget, requireID bool) error {
	if t == nil {
		return errors.New("nil target")
	}
	id := strings.TrimSpace(t.ID)
	if requireID && id == "" {
		return errors.New("id is required")
	}
	if id != "" && !warmTargetIDRe.MatchString(id) {
		return errors.New("id: use letters, digits, dot, underscore, hyphen (max 64 chars)")
	}
	if err := validateWarmTargetHost(t.Host); err != nil {
		return err
	}
	if u := strings.TrimSpace(t.SSHUser); u != "" {
		if !warmTargetIDRe.MatchString(u) {
			return errors.New("ssh_user: invalid characters")
		}
		t.SSHUser = u
	}
	t.ID = id
	t.Host = strings.TrimSpace(t.Host)
	t.Label = strings.TrimSpace(t.Label)
	t.Notes = strings.TrimSpace(t.Notes)
	return nil
}

func findWarmTargetByID(targets []warmTarget, id string) *warmTarget {
	id = strings.TrimSpace(id)
	for i := range targets {
		if targets[i].ID == id {
			return &targets[i]
		}
	}
	return nil
}

func handleRunpodWarmTargets(w http.ResponseWriter, r *http.Request) {
	if !authOK(r) {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}
	switch r.Method {
	case http.MethodGet:
		f, err := loadWarmTargets()
		if err != nil {
			jsonErr(w, http.StatusInternalServerError, err.Error())
			return
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{
			"path":    filepath.ToSlash(filepath.Join("configs", "training", ".wui_runpod_targets.json")),
			"targets": f.Targets,
		})
	case http.MethodPut:
		body, err := io.ReadAll(io.LimitReader(r.Body, 1<<20))
		if err != nil {
			jsonErr(w, http.StatusBadRequest, "read body")
			return
		}
		var in warmTargetsFile
		if err := json.Unmarshal(body, &in); err != nil {
			jsonErr(w, http.StatusBadRequest, "invalid JSON")
			return
		}
		seen := make(map[string]struct{})
		for i := range in.Targets {
			if err := validateWarmTargetEntry(&in.Targets[i], true); err != nil {
				jsonErr(w, http.StatusBadRequest, err.Error())
				return
			}
			id := in.Targets[i].ID
			if _, ok := seen[id]; ok {
				jsonErr(w, http.StatusBadRequest, "duplicate id: "+id)
				return
			}
			seen[id] = struct{}{}
			if strings.TrimSpace(in.Targets[i].UpdatedAt) == "" {
				in.Targets[i].UpdatedAt = time.Now().UTC().Format(time.RFC3339)
			}
		}
		if err := saveWarmTargets(&in); err != nil {
			jsonErr(w, http.StatusInternalServerError, err.Error())
			return
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "targets": in.Targets})
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}
