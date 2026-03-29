package edgeartifacts

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// TestBuildSynthetic_smoke runs a full synthetic build when wat2wasm is on PATH (Linux/macOS CI/dev).
func TestBuildSynthetic_smoke(t *testing.T) {
	if _, err := exec.LookPath("wat2wasm"); err != nil {
		t.Skip("wat2wasm not on PATH")
	}
	repoRoot := filepath.Clean(filepath.Join("..", ".."))
	if _, err := os.Stat(filepath.Join(repoRoot, "corpus", "trit_kernels.wat")); err != nil {
		repoRoot = filepath.Clean(filepath.Join("..", "..", ".."))
		if _, err2 := os.Stat(filepath.Join(repoRoot, "corpus", "trit_kernels.wat")); err2 != nil {
			t.Skip("corpus/trit_kernels.wat not found from test cwd")
		}
	}
	out := t.TempDir()
	log, err := Build(BuildOptions{RepoRoot: repoRoot, OutDir: out, Tier: 2, WasmOptLevel: "O3"})
	if err != nil {
		t.Log(log.String())
		t.Fatal(err)
	}
	if !strings.Contains(log.String(), "Wrote") {
		t.Fatalf("expected log output, got: %q", log.String())
	}
}
