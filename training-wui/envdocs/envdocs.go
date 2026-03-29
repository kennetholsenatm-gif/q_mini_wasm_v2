// Package envdocs generates docs/environment-variables.md from .env.schema (Varlock-style).
package envdocs

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// EnvVar is one row from .env.schema.
type EnvVar struct {
	Name        string
	Description string
	VarType     string
	Default     string
	Sensitive   bool
	Required    bool
}

const docIntro = "# Environment variables (secrets and APIs)\n\n" +
	"**Policy:** Variables documented here are **credentials and API keys** loaded from `.env` (or the process environment). They are **not** how you configure training, inference, accelerators, WASM limits, or feature toggles — use **`configs/training/*.toml`**, **`configs/serve/*.toml`**, **`configs/wui.toml`** (Training WUI process settings), and the **[Training WUI](../training-wui/README.md)** for that. See **[CONFIGURATION_POLICY.md](CONFIGURATION_POLICY.md)**.\n\n" +
	"For **CI, Docker, serverless, and toolchain** variables that may still be read by the codebase, see **[ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md)**.\n\n"

const docFooter = "## Usage notes\n\n" +
	"- Treat variables marked with 🔒 as **secrets**: never commit real values; use a secrets manager in production where applicable.\n" +
	"- Copy **[.env.example](../.env.example)** to `.env` for local development (`.env` is gitignored).\n" +
	"- **Do not** add training hyperparameters or runtime toggles to `.env` — add them to TOML or use the WUI.\n" +
	"- The **Training WUI** loads `.env` with an **allowlist** (secrets + `RUNPOD_SERVERLESS_ENDPOINT_ID`); other keys are ignored so configuration stays in **`configs/wui.toml`** / **`configs/runtime.toml`**.\n\n" +
	"## Related documentation\n\n" +
	"- [README](../README.md) — project overview\n" +
	"- [Training data](TRAINING_DATA.md)\n" +
	"- [ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md) — CI / container / legacy process env (not WUI user config)\n"

// ParseEnvSchema reads .env.schema and returns variables in file order.
func ParseEnvSchema(schemaFile string) ([]EnvVar, error) {
	raw, err := os.ReadFile(schemaFile)
	if err != nil {
		return nil, err
	}
	return ParseEnvSchemaBytes(string(raw)), nil
}

// ParseEnvSchemaBytes parses schema content (for tests).
func ParseEnvSchemaBytes(content string) []EnvVar {
	lines := strings.Split(content, "\n")
	var out []EnvVar
	descLines := []string{}
	pendingType := "string"
	pendingSensitive := false
	pendingDefault := ""

	for _, line := range lines {
		s := strings.TrimSpace(line)
		if s == "" {
			continue
		}
		if strings.HasPrefix(s, "#") && !strings.HasPrefix(s, "# @") {
			continue
		}

		switch {
		case strings.HasPrefix(s, "@env-spec"):
			descLines = nil
			pendingType = "string"
			pendingSensitive = false
			pendingDefault = ""
		case strings.HasPrefix(s, "@description"):
			t := strings.TrimSpace(strings.TrimPrefix(s, "@description"))
			if t != "" {
				descLines = append(descLines, t)
			}
		case strings.HasPrefix(s, "@type"):
			t := strings.TrimSpace(strings.TrimPrefix(s, "@type"))
			pendingType = t
		case strings.HasPrefix(s, "@default"):
			pendingDefault = strings.TrimSpace(strings.TrimPrefix(s, "@default"))
		case strings.HasPrefix(s, "@sensitive"):
			val := strings.TrimSpace(strings.TrimPrefix(s, "@sensitive"))
			pendingSensitive = strings.EqualFold(val, "true")
		case strings.Contains(s, "=") && !strings.HasPrefix(s, "@"):
			name := strings.TrimSpace(strings.SplitN(s, "=", 2)[0])
			v := EnvVar{
				Name:        name,
				VarType:     pendingType,
				Sensitive:   pendingSensitive,
				Default:     pendingDefault,
				Description: strings.Join(descLines, " "),
			}
			out = append(out, v)
			descLines = nil
			pendingType = "string"
			pendingSensitive = false
			pendingDefault = ""
		}
	}
	return out
}

func categoryFor(name string) string {
	u := strings.ToUpper(name)
	switch {
	case u == "HUGGING_FACE_HUB_TOKEN" || u == "HF_TOKEN":
		return "Hugging Face"
	case strings.Contains(u, "IBM_QUANTUM") || strings.Contains(u, "QISKIT_IBM"):
		return "IBM Quantum"
	case strings.Contains(u, "RUNPOD"):
		return "RunPod"
	default:
		return "Other"
	}
}

// GenerateMarkdown builds docs/environment-variables.md body (full file).
func GenerateMarkdown(envVars []EnvVar) string {
	if len(envVars) == 0 {
		return docIntro + "# Environment Variables\n\nNo environment variables found.\n" + docFooter
	}

	categories := map[string][]EnvVar{
		"Hugging Face": {},
		"IBM Quantum":  {},
		"RunPod":       {},
		"Other":        {},
	}
	for _, v := range envVars {
		c := categoryFor(v.Name)
		categories[c] = append(categories[c], v)
	}

	b := strings.Builder{}
	b.WriteString(docIntro)
	b.WriteString("## Table of Contents\n\n")
	order := []string{"Hugging Face", "IBM Quantum", "RunPod", "Other"}
	for _, cat := range order {
		if len(categories[cat]) == 0 {
			continue
		}
		anchor := strings.ToLower(strings.ReplaceAll(cat, " ", "-"))
		anchor = strings.ReplaceAll(anchor, "/", "-")
		b.WriteString(fmt.Sprintf("- [%s](#%s)\n", cat, anchor))
	}
	b.WriteString("\n")

	for _, cat := range order {
		list := categories[cat]
		if len(list) == 0 {
			continue
		}
		b.WriteString(fmt.Sprintf("## %s\n\n", cat))
		b.WriteString("| Variable | Type | Default | Required | Description |\n")
		b.WriteString("|----------|------|---------|----------|-------------|\n")

		names := make([]string, len(list))
		for i, v := range list {
			names[i] = v.Name
		}
		sort.Strings(names)
		byName := map[string]EnvVar{}
		for _, v := range list {
			byName[v.Name] = v
		}
		for _, n := range names {
			v := byName[n]
			req := "No"
			if v.Required {
				req = "Yes"
			}
			sens := ""
			if v.Sensitive {
				sens = " 🔒"
			}
			def := v.Default
			if def == "" {
				def = "None"
			}
			desc := v.Description
			if len(desc) > 80 {
				desc = desc[:77] + "..."
			}
			desc = strings.ReplaceAll(desc, "|", "\\|")
			b.WriteString(fmt.Sprintf("| `%s`%s | %s | `%s` | %s | %s |\n", v.Name, sens, v.VarType, def, req, desc))
		}
		b.WriteString("\n")
	}
	b.WriteString(docFooter)
	return b.String()
}

// WriteFromRepoRoot parses repoRoot/.env.schema and writes repoRoot/docs/environment-variables.md.
func WriteFromRepoRoot(repoRoot string) error {
	schema := filepath.Join(repoRoot, ".env.schema")
	vars, err := ParseEnvSchema(schema)
	if err != nil {
		return err
	}
	outPath := filepath.Join(repoRoot, "docs", "environment-variables.md")
	if err := os.MkdirAll(filepath.Dir(outPath), 0o755); err != nil {
		return err
	}
	return os.WriteFile(outPath, []byte(GenerateMarkdown(vars)), 0o644)
}
