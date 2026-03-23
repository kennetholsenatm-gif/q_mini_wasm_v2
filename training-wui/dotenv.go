package main

import (
	"bufio"
	"os"
	"path/filepath"
	"strings"
)

// loadDotenvFromRepo loads repoRoot/.env into the process environment.
// Existing OS environment variables are not overridden (same default as python-dotenv).
func loadDotenvFromRepo(repoRoot string) {
	path := filepath.Join(repoRoot, ".env")
	f, err := os.Open(path)
	if err != nil {
		return
	}
	defer f.Close()

	sc := bufio.NewScanner(f)
	// Allow long lines (values)
	const max = 1024 * 1024
	buf := make([]byte, 0, 64*1024)
	sc.Buffer(buf, max)

	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		if strings.HasPrefix(line, "export ") {
			line = strings.TrimSpace(strings.TrimPrefix(line, "export "))
		}
		i := strings.IndexByte(line, '=')
		if i <= 0 {
			continue
		}
		key := strings.TrimSpace(line[:i])
		if key == "" {
			continue
		}
		val := strings.TrimSpace(line[i+1:])
		val = unquoteEnvValue(val)
		if shouldSanitizeEnvKey(key) {
			val = sanitizeSecretValue(val)
		}
		if os.Getenv(key) == "" {
			_ = os.Setenv(key, val)
		}
	}
}

func shouldSanitizeEnvKey(key string) bool {
	k := strings.ToUpper(strings.TrimSpace(key))
	subs := []string{"TOKEN", "API_KEY", "APIKEY", "SECRET", "IBM_QUANTUM", "QISKIT", "HUGGING_FACE", "HF_", "RUNPOD"}
	for _, sub := range subs {
		if strings.Contains(k, sub) {
			return true
		}
	}
	return false
}

// sanitizeSecretValue strips accidental "…", '…', or {…} wrappers (IBM/HF reject those).
func sanitizeSecretValue(s string) string {
	s = strings.TrimSpace(s)
	for {
		t := strings.TrimSpace(s)
		if len(t) < 2 {
			break
		}
		if (t[0] == '"' && t[len(t)-1] == '"') || (t[0] == '\'' && t[len(t)-1] == '\'') {
			s = t[1 : len(t)-1]
			continue
		}
		if t[0] == '{' && t[len(t)-1] == '}' {
			s = t[1 : len(t)-1]
			continue
		}
		break
	}
	return strings.TrimSpace(s)
}

func unquoteEnvValue(s string) string {
	if len(s) >= 2 {
		if (s[0] == '"' && s[len(s)-1] == '"') || (s[0] == '\'' && s[len(s)-1] == '\'') {
			return s[1 : len(s)-1]
		}
	}
	return s
}
