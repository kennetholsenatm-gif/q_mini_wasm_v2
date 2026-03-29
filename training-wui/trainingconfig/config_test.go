package trainingconfig

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
	cfg, err := TrainingTOMLToProto(p, "run-abc", dir)
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
	cfg, err := TrainingTOMLToProto(p, "r1", dir)
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

func TestTrainingTOMLToProto_modelGeometryProto(t *testing.T) {
	t.Parallel()
	dir := t.TempDir()
	p := filepath.Join(dir, "cfg.toml")
	content := `
[training]
epochs = 1

[data]
path = "synthetic"

[model]
d_model = 2048
io_d_model = 4096
num_ternary_blocks = 3
`
	if err := os.WriteFile(p, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	cfg, err := TrainingTOMLToProto(p, "g1", dir)
	if err != nil {
		t.Fatal(err)
	}
	if cfg.GetDModel() != 2048 {
		t.Fatalf("d_model: %d", cfg.GetDModel())
	}
	if cfg.GetIoDModel() != 4096 {
		t.Fatalf("io_d_model: %d", cfg.GetIoDModel())
	}
	if cfg.GetNumTernaryBlocks() != 3 {
		t.Fatalf("num_ternary_blocks: %d", cfg.GetNumTernaryBlocks())
	}
}

func TestTrainingTOMLToProto_checkpointPathsAbsolute(t *testing.T) {
	t.Parallel()
	dir := t.TempDir()
	p := filepath.Join(dir, "cfg.toml")
	content := `
[training]
epochs = 2

[data]
path = "synthetic"

[checkpoint]
save_path = "artifacts/models/m/save.pt"
best_path = "artifacts/models/m/best.pt"
latest_path = "artifacts/models/m/latest.pt"

[tpem]
best_path = "artifacts/models/m/tpem_best.pt"
`
	if err := os.WriteFile(p, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	cfg, err := TrainingTOMLToProto(p, "run-x", dir)
	if err != nil {
		t.Fatal(err)
	}
	wantSave := filepath.Join(dir, "artifacts", "models", "m", "save.pt")
	wantLatest := filepath.Join(dir, "artifacts", "models", "m", "latest.pt")
	wantBest := filepath.Join(dir, "artifacts", "models", "m", "tpem_best.pt")
	if cfg.GetCheckpointSavePath() != wantSave {
		t.Fatalf("save: %q want %q", cfg.GetCheckpointSavePath(), wantSave)
	}
	if cfg.GetCheckpointLatestPath() != wantLatest {
		t.Fatalf("latest: %q want %q", cfg.GetCheckpointLatestPath(), wantLatest)
	}
	if cfg.GetCheckpointBestPath() != wantBest {
		t.Fatalf("best: %q want %q", cfg.GetCheckpointBestPath(), wantBest)
	}
}

func TestTrainingTOMLToProto_cascadeAndHf(t *testing.T) {
	t.Parallel()
	dir := t.TempDir()
	p := filepath.Join(dir, "cfg.toml")
	content := `
[training]
epochs = 4
batch_size = 8
learning_rate = 0.0001
seed = 99

[data]
source = "hf_tabular"
path = "code-search-net/code_search_net"

[huggingface]
dataset_config = "python"
split = "train"
num_samples = 100
mesh_blend_fraction = 0.1

[cascade_curriculum_loop]
enabled = true
max_heal_rounds = 2
teacher_checkpoint_path = "artifacts/models/cascade/teacher.pt"
ptqtp_num_planes = 2
gate_target_val_mse = 0.02
gate_max_tpem_mib = 64
run_taxonomy_linter = false
heal_learning_rate_scale = 0.75
teacher_epoch_fraction = 0.5
heal_epochs_per_round = 1
max_curriculum_cycles = 2

[checkpoint]
save_path = "artifacts/models/cascade/final.pt"
latest_path = "artifacts/models/cascade/latest.pt"

[model]
d_model = 128
io_d_model = 128
num_ternary_blocks = 1
`
	if err := os.WriteFile(p, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	cfg, err := TrainingTOMLToProto(p, "c1", dir)
	if err != nil {
		t.Fatal(err)
	}
	if !cfg.GetUseNativeEngineOnly() {
		t.Fatal("expected use_native_engine_only")
	}
	cl := cfg.GetCascadeLoop()
	if cl == nil || !cl.GetEnabled() {
		t.Fatalf("cascade: %+v", cl)
	}
	if cl.GetMaxHealRounds() != 2 || cl.GetPtqtpNumPlanes() != 2 || cl.GetMaxCurriculumCycles() != 2 {
		t.Fatalf("cascade fields: %+v", cl)
	}
	wantTeacher := filepath.Join(dir, "artifacts", "models", "cascade", "teacher.pt")
	if cl.GetTeacherCheckpointPath() != wantTeacher {
		t.Fatalf("teacher path: %q want %q", cl.GetTeacherCheckpointPath(), wantTeacher)
	}
	hf := cfg.GetHf()
	if hf == nil || hf.GetDatasetId() != "code-search-net/code_search_net" {
		t.Fatalf("hf: %+v", hf)
	}
	if hf.GetConfigName() != "python" || hf.GetNumSamples() != 100 {
		t.Fatalf("hf fields: %+v", hf)
	}
	native, err := TrainingTomlCascadeRequiresNativeGRPC(p)
	if err != nil || !native {
		t.Fatalf("TrainingTomlCascadeRequiresNativeGRPC: %v %v", native, err)
	}
	if cfg.GetBatchSize() != 32 || cfg.GetMicroBatchSize() != 32 {
		t.Fatalf("cascade auto batch: got batch=%d micro=%d want 32/32", cfg.GetBatchSize(), cfg.GetMicroBatchSize())
	}
}

func TestTrainingTOMLToProto_cascadeOmittedGateTargetDisablesMseGate(t *testing.T) {
	t.Parallel()
	dir := t.TempDir()
	p := filepath.Join(dir, "cfg.toml")
	content := `
[training]
epochs = 2
batch_size = 4
learning_rate = 0.001

[cascade_curriculum_loop]
enabled = true
teacher_checkpoint_path = "artifacts/models/t/teacher.pt"

[checkpoint]
save_path = "artifacts/models/t/final.pt"

[model]
d_model = 32
io_d_model = 32
num_ternary_blocks = 1
`
	if err := os.WriteFile(p, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	cfg, err := TrainingTOMLToProto(p, "g0", dir)
	if err != nil {
		t.Fatal(err)
	}
	cl := cfg.GetCascadeLoop()
	if cl == nil || !cl.GetEnabled() {
		t.Fatalf("cascade: %+v", cl)
	}
	if cl.GetGateTargetValMse() != 0 {
		t.Fatalf("omitted gate_target_val_mse: got %g want 0 (MSE gate disabled)", cl.GetGateTargetValMse())
	}
	if cfg.GetBatchSize() != 32 {
		t.Fatalf("cascade auto batch: got %d want 32 (TOML batch_size=4 ignored)", cfg.GetBatchSize())
	}
}

func TestInferNativeCascadeBatchSize_macroGeometry(t *testing.T) {
	t.Parallel()
	if g := InferNativeCascadeBatchSize(8192, 8192, 15); g != 32 {
		t.Fatalf("got %d want 32", g)
	}
	if g := InferNativeCascadeBatchSize(128, 128, 1); g != 32 {
		t.Fatalf("small model got %d want 32", g)
	}
}

func TestCascadeNativeStepsPerEpochCeil(t *testing.T) {
	t.Parallel()
	cases := []struct {
		rows, bs, want int
	}{
		{10, 3, 4},
		{100, 10, 10},
		{1, 8, 1},
		{8, 8, 1},
		{9, 8, 2},
	}
	for _, tc := range cases {
		steps := (tc.rows + tc.bs - 1) / tc.bs
		if steps < 1 {
			steps = 1
		}
		if steps != tc.want {
			t.Fatalf("rows=%d bs=%d got %d want %d", tc.rows, tc.bs, steps, tc.want)
		}
	}
}
