package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestBuildEdgeTomlOverlay_tier1(t *testing.T) {
	td := t.TempDir()
	base := filepath.Join(td, "base.toml")
	if err := os.WriteFile(base, []byte("[training]\nbatch_size = 64\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	req := &TrainingRequest{EnclaveTier: 1, MemoryLimitMB: 128}
	s, err := buildEdgeTomlOverlay(req, base)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(s, "use_tsign_ternary = true") {
		t.Fatalf("missing tsign: %q", s)
	}
	if !strings.Contains(s, "enclave_tier = \"micro\"") {
		t.Fatalf("missing micro: %q", s)
	}
	if !strings.Contains(s, "batch_size = 16") {
		t.Fatalf("expected batch downscale: %q", s)
	}
}

func TestBuildEdgeTomlOverlay_tier4_defaultsFootprintWhenMemoryUnset(t *testing.T) {
	td := t.TempDir()
	base := filepath.Join(td, "base.toml")
	if err := os.WriteFile(base, []byte("[training]\nbatch_size = 16\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	req := &TrainingRequest{EnclaveTier: 4, MemoryLimitMB: 0}
	s, err := buildEdgeTomlOverlay(req, base)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(s, "wasm_memory64_max_mb = 16384") {
		t.Fatalf("expected default macro-class footprint for tier 4: %q", s)
	}
}

func TestBuildEdgeTomlOverlay_quantumMesh(t *testing.T) {
	td := t.TempDir()
	base := filepath.Join(td, "base.toml")
	if err := os.WriteFile(base, []byte("[training]\nbatch_size = 32\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	req := &TrainingRequest{EnclaveTier: 2, MemoryLimitMB: 0, HardwareAccelerator: "quantum_mesh"}
	s, err := buildEdgeTomlOverlay(req, base)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(s, `accelerator = "cpu"`) {
		t.Fatal(s)
	}
	if !strings.Contains(s, "quantum_execution_policy") {
		t.Fatal(s)
	}
	env := buildEdgeProfileExtraEnv(req)
	found := false
	for _, e := range env {
		if e == "QMW_QUANTUM_MESH_ROUTING=1" {
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("expected mesh env in %v", env)
	}
}

func TestBuildEdgeTomlOverlay_tier3(t *testing.T) {
	td := t.TempDir()
	base := filepath.Join(td, "base.toml")
	if err := os.WriteFile(base, []byte("[training]\nbatch_size = 32\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	req := &TrainingRequest{EnclaveTier: 3, MemoryLimitMB: 8192}
	s, err := buildEdgeTomlOverlay(req, base)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(s, "use_memory64 = true") {
		t.Fatal(s)
	}
	if !strings.Contains(s, "enclave_tier = \"macro\"") {
		t.Fatal(s)
	}
	if !strings.Contains(s, "batch_size = 64") {
		t.Fatalf("expected 2x batch: %q", s)
	}
}

func TestMergeTrainingTomlBytes(t *testing.T) {
	base := []byte("[training]\nepochs = 10\nbatch_size = 32\n\n[adapter]\nhybrid_adapter = true\n")
	over := `[adapter]
use_tsign_ternary = true
[enclave]
enclave_tier = "micro"
`
	out, err := mergeTrainingTomlBytes(base, over)
	if err != nil {
		t.Fatal(err)
	}
	s := string(out)
	if !strings.Contains(s, "use_tsign_ternary = true") {
		t.Fatal(s)
	}
	if !strings.Contains(s, "epochs = 10") {
		t.Fatal(s)
	}
}
