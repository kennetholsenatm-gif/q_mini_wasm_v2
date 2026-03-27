package main

import (
	"path/filepath"
	"testing"
)

func TestResolveRepoRelativePath(t *testing.T) {
	root := filepath.Clean(t.TempDir())
	got, err := resolveRepoRelativePath(root, "artifacts/models/x/final.pt")
	if err != nil {
		t.Fatal(err)
	}
	want := filepath.Join(root, "artifacts", "models", "x", "final.pt")
	if filepath.Clean(got) != filepath.Clean(want) {
		t.Fatalf("got %q want %q", got, want)
	}
	if _, err := resolveRepoRelativePath(root, "../escape"); err == nil {
		t.Fatal("expected error for path escape")
	}
	if _, err := resolveRepoRelativePath(root, ""); err == nil {
		t.Fatal("expected error for empty path")
	}
}
