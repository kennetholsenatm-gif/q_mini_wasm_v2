package main

import (
	"bufio"
	"os"
	"path/filepath"
	"strings"
)

const pathMapRel = "config/path_map.toml"

// pathMap holds optional layout overrides from config/path_map.toml (minimal parser).
type pathMap struct {
	QminiwasmExe        string
	WuiDir              string
	NativeRuntimeDir    string
	QTrainingDLL        string
}

func defaultPathMap() *pathMap {
	return &pathMap{
		QminiwasmExe:     "qminiwasm.exe",
		WuiDir:           "wui",
		NativeRuntimeDir: "native_runtime",
		QTrainingDLL:     "q_training.dll",
	}
}

// findRepoRoot walks upward from startDir looking for go.mod (module root).
func findRepoRoot(startDir string) string {
	dir := startDir
	for i := 0; i < 12; i++ {
		gm := filepath.Join(dir, "go.mod")
		if st, err := os.Stat(gm); err == nil && !st.IsDir() {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			break
		}
		dir = parent
	}
	return ""
}

// loadPathMap reads config/path_map.toml from repoRoot. Unknown keys are ignored.
func loadPathMap(repoRoot string) *pathMap {
	out := defaultPathMap()
	if repoRoot == "" {
		return out
	}
	p := filepath.Join(repoRoot, pathMapRel)
	f, err := os.Open(p)
	if err != nil {
		return out
	}
	defer f.Close()

	inPaths := false
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			inPaths = strings.EqualFold(line, "[paths]")
			continue
		}
		if !inPaths {
			continue
		}
		key, val, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}
		key = strings.TrimSpace(key)
		val = strings.TrimSpace(val)
		val = strings.Trim(val, `"'`)
		switch key {
		case "qminiwasm_exe":
			if val != "" {
				out.QminiwasmExe = val
			}
		case "wui_dir":
			if val != "" {
				out.WuiDir = val
			}
		case "native_runtime_dir":
			if val != "" {
				out.NativeRuntimeDir = val
			}
		case "q_training_dll":
			if val != "" {
				out.QTrainingDLL = val
			}
		}
	}
	return out
}

func canonicalExePath(repoRoot string, pm *pathMap) string {
	if repoRoot == "" || pm == nil {
		return ""
	}
	return filepath.Join(repoRoot, pm.QminiwasmExe)
}
