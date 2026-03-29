package edgeartifacts

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
)

var wasmOptLevels = map[string]bool{
	"O0": true, "O1": true, "O2": true, "O3": true, "Os": true, "Oz": true,
}

// CompileTritKernelsWasm runs wat2wasm on corpus/trit_kernels.wat under repoRoot.
func CompileTritKernelsWasm(repoRoot string) ([]byte, error) {
	wat := filepath.Join(repoRoot, "corpus", "trit_kernels.wat")
	if _, err := os.Stat(wat); err != nil {
		return nil, fmt.Errorf("trit kernels wat: %w", err)
	}
	w2w, err := exec.LookPath("wat2wasm")
	if err != nil {
		return nil, fmt.Errorf("wat2wasm not on PATH (install wabt): %w", err)
	}
	tmp, err := os.CreateTemp("", "qmw-kernels-*.wasm")
	if err != nil {
		return nil, err
	}
	outPath := tmp.Name()
	_ = tmp.Close()
	defer os.Remove(outPath)
	cmd := exec.Command(w2w, wat, "-o", outPath)
	if out, err := cmd.CombinedOutput(); err != nil {
		return nil, fmt.Errorf("wat2wasm: %w: %s", err, string(out))
	}
	return os.ReadFile(outPath)
}

// MaybeWasmOptTier1 runs wasm-opt for tier==1 when available; level like "O3".
func MaybeWasmOptTier1(wasm []byte, tier int, optLevel string) ([]byte, string) {
	if tier != 1 {
		return wasm, ""
	}
	level := optLevel
	if level == "" {
		level = "O3"
	}
	if !wasmOptLevels[level] {
		return wasm, fmt.Sprintf("invalid wasm-opt level %q; skipping", level)
	}
	if _, err := exec.LookPath("wasm-opt"); err != nil {
		return wasm, "wasm-opt not on PATH; skipping tier-1 optimization"
	}
	tmpDir, err := os.MkdirTemp("", "qmw_wasm_opt_*")
	if err != nil {
		return wasm, fmt.Sprintf("wasm-opt temp dir: %v; using unoptimized wasm", err)
	}
	defer os.RemoveAll(tmpDir)
	inPath := filepath.Join(tmpDir, "in.wasm")
	outPath := filepath.Join(tmpDir, "out.wasm")
	if err := os.WriteFile(inPath, wasm, 0o644); err != nil {
		return wasm, fmt.Sprintf("wasm-opt write: %v", err)
	}
	flag := "-" + level
	cmd := exec.Command("wasm-opt", flag, inPath, "-o", outPath)
	if out, err := cmd.CombinedOutput(); err != nil {
		return wasm, fmt.Sprintf("wasm-opt failed (%v): %s; using unoptimized wasm", err, string(out))
	}
	b, err := os.ReadFile(outPath)
	if err != nil || len(b) == 0 {
		return wasm, "wasm-opt output missing; using unoptimized wasm"
	}
	return b, ""
}

// WasmOptLevels returns sorted valid level names (for help text).
func WasmOptLevels() []string {
	s := make([]string, 0, len(wasmOptLevels))
	for k := range wasmOptLevels {
		s = append(s, k)
	}
	slices.Sort(s)
	return s
}
