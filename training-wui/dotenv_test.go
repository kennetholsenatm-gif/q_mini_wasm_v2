package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestSanitizeSecretValue(t *testing.T) {
	cases := []struct{ in, want string }{
		{`"abc"`, `abc`},
		{`'x'`, `x`},
		{`{tok}`, `tok`},
		{`"{nested}"`, `nested`},
		{`{ "spaced" }`, `spaced`},
	}
	for _, c := range cases {
		if got := sanitizeSecretValue(c.in); got != c.want {
			t.Fatalf("sanitizeSecretValue(%q) = %q want %q", c.in, got, c.want)
		}
	}
}

func TestLoadDotenvFromRepo(t *testing.T) {
	dir := t.TempDir()
	_ = os.Unsetenv("HUGGING_FACE_HUB_TOKEN")
	_ = os.Unsetenv("RUNPOD_SERVERLESS_ENDPOINT_ID")
	_ = os.Unsetenv("QMINIWASM_SHOULD_IGNORE")
	_ = os.Setenv("HUGGING_FACE_HUB_TOKEN", "keep")
	content := "# comment\nHUGGING_FACE_HUB_TOKEN=new\nexport RUNPOD_SERVERLESS_ENDPOINT_ID=ep1\nQMINIWASM_SHOULD_IGNORE=bad\n"
	if err := os.WriteFile(filepath.Join(dir, ".env"), []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	loadDotenvFromRepo(dir)
	if os.Getenv("HUGGING_FACE_HUB_TOKEN") != "keep" {
		t.Fatalf("existing secret env should not be overridden, got %q", os.Getenv("HUGGING_FACE_HUB_TOKEN"))
	}
	if os.Getenv("RUNPOD_SERVERLESS_ENDPOINT_ID") != "ep1" {
		t.Fatalf("RUNPOD_SERVERLESS_ENDPOINT_ID=%q", os.Getenv("RUNPOD_SERVERLESS_ENDPOINT_ID"))
	}
	if os.Getenv("QMINIWASM_SHOULD_IGNORE") != "" {
		t.Fatalf("non-allowlisted key should be ignored, got %q", os.Getenv("QMINIWASM_SHOULD_IGNORE"))
	}
}
