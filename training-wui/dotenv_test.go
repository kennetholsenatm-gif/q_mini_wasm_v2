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
	_ = os.Unsetenv("FOO_TEST")
	_ = os.Unsetenv("BAR_TEST")
	_ = os.Setenv("EXISTING", "keep")
	content := "# comment\nFOO_TEST=hello\nexport BAR_TEST=world\nEXISTING=from-env\n"
	if err := os.WriteFile(filepath.Join(dir, ".env"), []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	loadDotenvFromRepo(dir)
	if os.Getenv("FOO_TEST") != "hello" {
		t.Fatalf("FOO_TEST=%q", os.Getenv("FOO_TEST"))
	}
	if os.Getenv("BAR_TEST") != "world" {
		t.Fatalf("BAR_TEST=%q", os.Getenv("BAR_TEST"))
	}
	if os.Getenv("EXISTING") != "keep" {
		t.Fatalf("existing env should not be overridden, got %q", os.Getenv("EXISTING"))
	}
}
