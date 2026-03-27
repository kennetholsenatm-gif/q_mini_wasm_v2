package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestResolveEdgeArtifactDownloadPath(t *testing.T) {
	root := t.TempDir()
	base := filepath.Join(root, "dist", "edge-artifacts", "build-abc")
	if err := os.MkdirAll(base, 0o755); err != nil {
		t.Fatal(err)
	}
	goodRel := filepath.ToSlash(filepath.Join("dist", "edge-artifacts", "build-abc", "qminiwasm-edge-bundle.zip"))
	abs, err := resolveEdgeArtifactDownloadPath(root, goodRel)
	if err != nil {
		t.Fatal(err)
	}
	if abs != filepath.Join(base, "qminiwasm-edge-bundle.zip") {
		t.Fatalf("abs=%q", abs)
	}

	if _, err := resolveEdgeArtifactDownloadPath(root, ""); err == nil {
		t.Fatal("expected error for empty path")
	}
	if _, err := resolveEdgeArtifactDownloadPath(root, "../escape"); err == nil {
		t.Fatal("expected error for escape")
	}
	if _, err := resolveEdgeArtifactDownloadPath(root, "artifacts/models/foo.pt"); err == nil {
		t.Fatal("expected error outside edge-artifacts")
	}
}
