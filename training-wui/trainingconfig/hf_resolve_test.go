package trainingconfig

import (
	"context"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"

	toml "github.com/pelletier/go-toml/v2"
)

func TestResolveHFDefaultConfigName_prefersAll(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/info" {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"dataset_info":{"ruby":{},"all":{},"python":{}}}`))
	}))
	defer srv.Close()
	t.Setenv("QMW_HF_DATASETS_SERVER_BASE", srv.URL)
	got, err := ResolveHFDefaultConfigName(context.Background(), "code-search-net/code_search_net")
	if err != nil {
		t.Fatal(err)
	}
	if got != "all" {
		t.Fatalf("got %q want all", got)
	}
}

func TestBuildHfProto_emptyDatasetConfig_resolvesFromMockInfo(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"dataset_info":{"python":{},"java":{}}}`))
	}))
	defer srv.Close()
	t.Setenv("QMW_HF_DATASETS_SERVER_BASE", srv.URL)

	dir := t.TempDir()
	p := filepath.Join(dir, "cfg.toml")
	content := `
[data]
source = "hf_tabular"
path = "code-search-net/code_search_net"

[huggingface]
split = "train"
num_samples = 10
`
	if err := os.WriteFile(p, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	var doc TrainingDoc
	raw, _ := os.ReadFile(p)
	if err := toml.Unmarshal(raw, &doc); err != nil {
		t.Fatal(err)
	}
	hf, err := BuildHfProto(&doc, dir)
	if err != nil {
		t.Fatal(err)
	}
	if hf == nil {
		t.Fatal("nil hf")
	}
	if hf.ConfigName != "java" {
		t.Fatalf("ConfigName %q want java", hf.ConfigName)
	}
}
