package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestRunpodServerlessDefaultWorkerImageIsAlmaLinux10(t *testing.T) {
	got := runpodServerlessDefaultWorkerImage()
	if !strings.Contains(strings.ToLower(got), "alma") {
		t.Fatalf("expected AlmaLinux-based default image, got %q", got)
	}
}

func TestWorkerImageFromTrainingConfigAbsUsesTomlOverride(t *testing.T) {
	dir := t.TempDir()
	cfg := filepath.Join(dir, "train.toml")
	err := os.WriteFile(cfg, []byte(`
[runpod_serverless]
worker_image = "docker.io/example/alma10-worker:v1"
`), 0o644)
	if err != nil {
		t.Fatal(err)
	}

	image, source, err := workerImageFromTrainingConfigAbs(cfg)
	if err != nil {
		t.Fatal(err)
	}
	if source != "toml" {
		t.Fatalf("expected source toml, got %q", source)
	}
	if image != "docker.io/example/alma10-worker:v1" {
		t.Fatalf("unexpected image %q", image)
	}
}

