package envdocs

import (
	"strings"
	"testing"
)

func TestParseEnvSchemaBytes_orderAndFields(t *testing.T) {
	const sample = `
# comment
@env-spec
@type string
@description First token
@sensitive true
HUGGING_FACE_HUB_TOKEN=

@env-spec
@description Second thing
OTHER_THING=
`
	vars := ParseEnvSchemaBytes(sample)
	if len(vars) != 2 {
		t.Fatalf("got %d vars, want 2", len(vars))
	}
	if vars[0].Name != "HUGGING_FACE_HUB_TOKEN" || !vars[0].Sensitive || vars[0].VarType != "string" {
		t.Fatalf("first var: %+v", vars[0])
	}
	if vars[1].Name != "OTHER_THING" || vars[1].Description != "Second thing" {
		t.Fatalf("second var: %+v", vars[1])
	}
}

func TestGenerateMarkdown_containsCategories(t *testing.T) {
	md := GenerateMarkdown([]EnvVar{
		{Name: "HF_TOKEN", VarType: "string", Sensitive: true, Description: "alias"},
		{Name: "RUNPOD_TOKEN", VarType: "string", Sensitive: true, Description: "key"},
	})
	if !strings.Contains(md, "## Hugging Face") || !strings.Contains(md, "## RunPod") {
		snippet := md
		if len(snippet) > 200 {
			snippet = snippet[:200]
		}
		t.Fatalf("missing section: %s", snippet)
	}
	if !strings.Contains(md, "`HF_TOKEN`") {
		t.Fatal("missing row")
	}
}
