package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	toml "github.com/pelletier/go-toml/v2"
)

func TestForceEnclaveTierInToml(t *testing.T) {
	base := []byte("[enclave]\nenclave_tier = 2\n")
	out, err := forceEnclaveTierInToml(base, 4)
	if err != nil {
		t.Fatal(err)
	}
	var root map[string]any
	if err := toml.Unmarshal(out, &root); err != nil {
		t.Fatal(err)
	}
	enc, _ := root["enclave"].(map[string]any)
	if enc == nil {
		t.Fatal("missing enclave")
	}
	if enc["enclave_tier"] != int64(4) {
		t.Fatalf("got enclave_tier %v", enc["enclave_tier"])
	}
}

func TestApplyDatasetMixToMergedToml_zeroSpecs(t *testing.T) {
	merged := []byte(`[data]
path = "code-search-net/code_search_net"

[huggingface]
num_samples = 2048
split = "train"
`)
	out, applied, first, err := applyDatasetMixToMergedToml(merged, nil, 0)
	if err != nil {
		t.Fatal(err)
	}
	if applied || first {
		t.Fatalf("expected no mix overlay, got applied=%v first=%v", applied, first)
	}
	if string(out) != string(merged) {
		t.Fatalf("expected unchanged bytes")
	}
}

func TestApplyDatasetMixToMergedToml_oneSpec(t *testing.T) {
	merged := []byte(`[data]
path = "code-search-net/code_search_net"

[huggingface]
num_samples = 4096
split = "train"
`)
	specs := []nativeHFSpec{{Path: "openai/gsm8k", DatasetConfig: ""}}
	out, applied, first, err := applyDatasetMixToMergedToml(merged, specs, 1000)
	if err != nil {
		t.Fatal(err)
	}
	if !applied || first {
		t.Fatalf("applied=%v first=%v", applied, first)
	}
	s := string(out)
	if !strings.Contains(s, "openai/gsm8k") {
		t.Fatalf("missing gsm8k path: %s", s)
	}
	// min(4096, 1000) = 1000
	if !strings.Contains(s, "num_samples = 1000") {
		t.Fatalf("expected num_samples 1000, got: %s", s)
	}
}

func TestApplyDatasetMixToMergedToml_twoSpecs(t *testing.T) {
	merged := []byte(`[huggingface]
num_samples = 500
`)
	specs := []nativeHFSpec{
		{Path: "a/one", DatasetConfig: "cfg1"},
		{Path: "b/two", DatasetConfig: ""},
	}
	out, applied, first, err := applyDatasetMixToMergedToml(merged, specs, 0)
	if err != nil {
		t.Fatal(err)
	}
	if !applied || !first {
		t.Fatalf("applied=%v first=%v", applied, first)
	}
	s := string(out)
	if !strings.Contains(s, "qminiwasm/hf-multi") {
		t.Fatal(s)
	}
	if !strings.Contains(s, "a/one") || !strings.Contains(s, "b/two") {
		t.Fatal(s)
	}
	if !strings.Contains(s, "cfg1") {
		t.Fatal(s)
	}
}

func TestMergeTrainingTomlFromFiles(t *testing.T) {
	dir := t.TempDir()
	base := filepath.Join(dir, "base.toml")
	over := filepath.Join(dir, "over.toml")
	if err := os.WriteFile(base, []byte("[training]\nepochs = 4\n[huggingface]\nnum_samples = 1\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(over, []byte("[huggingface]\nnum_samples = 99\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	merged, err := mergeTrainingTomlFromFiles(base, over)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(merged), "num_samples = 99") {
		t.Fatalf("overlay did not win: %s", string(merged))
	}
}
