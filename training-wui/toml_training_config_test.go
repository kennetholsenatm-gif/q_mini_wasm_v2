package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestTrainingTOMLToProto_minimal(t *testing.T) {
	t.Parallel()
	dir := t.TempDir()
	p := filepath.Join(dir, "cfg.toml")
	content := `
[training]
epochs = 3
batch_size = 32
learning_rate = 0.01
seed = 7

[data]
path = "data/corpus.bin"
`
	if err := os.WriteFile(p, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	cfg, err := TrainingTOMLToProto(p, "run-abc")
	if err != nil {
		t.Fatal(err)
	}
	if cfg.GetRunId() != "run-abc" {
		t.Fatalf("run_id: %q", cfg.GetRunId())
	}
	if cfg.GetDatasetUri() != "data/corpus.bin" {
		t.Fatalf("dataset_uri: %q", cfg.GetDatasetUri())
	}
	if cfg.GetEpochs() != 3 || cfg.GetBatchSize() != 32 {
		t.Fatalf("epochs/batch: %d %d", cfg.GetEpochs(), cfg.GetBatchSize())
	}
	if cfg.GetLearningRate() != 0.01 || cfg.GetSeed() != 7 {
		t.Fatalf("lr/seed: %v %d", cfg.GetLearningRate(), cfg.GetSeed())
	}
	if cfg.GetTaxonomyTier() != "" {
		t.Fatalf("taxonomy: %q", cfg.GetTaxonomyTier())
	}
	if cfg.GetPrecisionPolicy() != "fp32" {
		t.Fatalf("precision: %q", cfg.GetPrecisionPolicy())
	}
}

func TestTrainingTOMLToProto_enclaveAdapterHF(t *testing.T) {
	t.Parallel()
	dir := t.TempDir()
	p := filepath.Join(dir, "cfg.toml")
	content := `
[training]
epochs = 1

[data]
path = "qminiwasm/hf-multi"

[[huggingface.extra_specs]]
path = "org/dataset"
dataset_config = "default"

[checkpoint]
load_path = "artifacts/models/x/best.pt"

[enclave]
enclave_tier = "meso"

[adapter]
use_tsign_ternary = true
`
	if err := os.WriteFile(p, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	cfg, err := TrainingTOMLToProto(p, "r1")
	if err != nil {
		t.Fatal(err)
	}
	if want := "org/dataset:default"; cfg.GetDatasetUri() != want {
		t.Fatalf("dataset_uri got %q want %q", cfg.GetDatasetUri(), want)
	}
	if cfg.GetModelUri() != "artifacts/models/x/best.pt" {
		t.Fatalf("model_uri: %q", cfg.GetModelUri())
	}
	if cfg.GetTaxonomyTier() != "meso" {
		t.Fatalf("taxonomy: %q", cfg.GetTaxonomyTier())
	}
	if cfg.GetPrecisionPolicy() != "ternary" {
		t.Fatalf("precision: %q", cfg.GetPrecisionPolicy())
	}
}
