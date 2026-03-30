package trainingconfig

import (
	"context"
	"errors"
	"fmt"
	"os"
	"strings"
	"time"

	toml "github.com/pelletier/go-toml/v2"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingrpc"
)

// InferNativeCascadeBatchSize matches cpp/training/src/grpc/training_engine_service.cpp
// FillNativeColdStartMemoryEstimate activation_scratch_bytes_hint: batch * (io + d*(nb+2)) * 4 * 2.
func InferNativeCascadeBatchSize(dModel, ioDModel, numBlocks uint32) uint32 {
	d := dModel
	if d == 0 {
		d = 256
	}
	io := ioDModel
	if io == 0 {
		io = d
	}
	nb := numBlocks
	if nb == 0 {
		nb = 1
	}
	per := uint64(io) + uint64(d)*uint64(nb+2)
	if per == 0 {
		return 1
	}
	const budgetBytes = 256 << 20
	denom := per * 8
	bs := budgetBytes / denom
	if bs < 1 {
		bs = 1
	}
	const maxBatch = 32
	if bs > maxBatch {
		bs = maxBatch
	}
	return uint32(bs)
}

// TrainingDoc is a minimal TOML shape for mapping training configs to the C++ gRPC TrainingConfig.
type TrainingDoc struct {
	Hardware struct {
		Accelerator    string `toml:"accelerator"`
		QuantumBackend string `toml:"quantum_backend"`
	} `toml:"hardware"`
	Training struct {
		Epochs        *int64   `toml:"epochs"`
		BatchSize     *int64   `toml:"batch_size"`
		LearningRate  *float64 `toml:"learning_rate"`
		Seed          *int64   `toml:"seed"`
		GradClipNorm  *float64 `toml:"grad_clip_norm"`
		MicroBatch    *int64   `toml:"micro_batch_size"`
		PrefetchDepth *int64   `toml:"prefetch_depth"`
		ComputeSlots  *int64   `toml:"compute_slots"`
		WorkerThreads *int64   `toml:"worker_threads"`
	} `toml:"training"`
	Data struct {
		Path   string `toml:"path"`
		Source string `toml:"source"`
	} `toml:"data"`
	HuggingFace struct {
		DatasetConfig     *string       `toml:"dataset_config"`
		Split             *string       `toml:"split"`
		NumSamples        *int64        `toml:"num_samples"`
		DatasetRevision   *string       `toml:"dataset_revision"`
		MeshBlendFraction *float64      `toml:"mesh_blend_fraction"`
		ExtraSpecs        []hfExtraToml `toml:"extra_specs"`
	} `toml:"huggingface"`
	Checkpoint struct {
		LoadPath   string `toml:"load_path"`
		SavePath   string `toml:"save_path"`
		BestPath   string `toml:"best_path"`
		LatestPath string `toml:"latest_path"`
	} `toml:"checkpoint"`
	Tpem struct {
		LoadPath   string `toml:"load_path"`
		SavePath   string `toml:"save_path"`
		BestPath   string `toml:"best_path"`
		LatestPath string `toml:"latest_path"`
	} `toml:"tpem"`
	Enclave struct {
		EnclaveTier any `toml:"enclave_tier"`
	} `toml:"enclave"`
	Adapter struct {
		UseTsignTernary *bool `toml:"use_tsign_ternary"`
	} `toml:"adapter"`
	Model struct {
		DModel              *int64  `toml:"d_model"`
		IoDModel            *int64  `toml:"io_d_model"`
		NumTernaryBlocks    *int64  `toml:"num_ternary_blocks"`
		AttentionBackend    *string `toml:"attention_backend"`
		NativeBlochSeqLen   *int64  `toml:"native_bloch_seq_len"`
		NativeBlochNumHeads *int64  `toml:"native_bloch_num_heads"`
	} `toml:"model"`
	Cascade struct {
		PolicyOptimizer   *string  `toml:"policy_optimizer"`
		CispoClipEpsilon  *float64 `toml:"cispo_clip_epsilon"`
		GroupSize         *int64   `toml:"group_size"`
	} `toml:"cascade"`
	TrainingPhases        []trainingPhaseToml `toml:"training_phases"`
	CascadeCurriculumLoop struct {
		Enabled               *bool    `toml:"enabled"`
		MaxHealRounds         *int64   `toml:"max_heal_rounds"`
		TeacherCheckpointPath string   `toml:"teacher_checkpoint_path"`
		PtqtpNumPlanes        *int64   `toml:"ptqtp_num_planes"`
		GateTargetValMse      *float64 `toml:"gate_target_val_mse"`
		GateMaxTpemMib        *float64 `toml:"gate_max_tpem_mib"`
		RunTaxonomyLinter     *bool    `toml:"run_taxonomy_linter"`
		HealLearningRateScale *float64 `toml:"heal_learning_rate_scale"`
		TeacherEpochFraction  *float64 `toml:"teacher_epoch_fraction"`
		HealEpochsPerRound    *int64   `toml:"heal_epochs_per_round"`
		MaxCurriculumCycles   *int64   `toml:"max_curriculum_cycles"`
	} `toml:"cascade_curriculum_loop"`
}

type hfExtraToml struct {
	Path          string `toml:"path"`
	DatasetConfig string `toml:"dataset_config"`
}

// ParseTrainingDocFile loads a training TOML for inspection (checkpoint paths, cascade flags).
func ParseTrainingDocFile(abs string) (TrainingDoc, error) {
	var doc TrainingDoc
	raw, err := os.ReadFile(abs)
	if err != nil {
		return doc, err
	}
	if err := toml.Unmarshal(raw, &doc); err != nil {
		return doc, err
	}
	return doc, nil
}

func u32Or(v *int64, def uint32) uint32 {
	if v == nil || *v < 0 {
		return def
	}
	return uint32(*v)
}

func f64Or(v *float64, def float64) float64 {
	if v == nil {
		return def
	}
	return *v
}

func u64Seed(v *int64, def uint64) uint64 {
	if v == nil || *v < 0 {
		return def
	}
	return uint64(*v)
}

func enclaveTierToTaxonomy(raw any, enclaveTierStr string) string {
	s := strings.TrimSpace(enclaveTierStr)
	if s == "" && raw != nil {
		switch x := raw.(type) {
		case int64:
			switch x {
			case 1:
				s = "micro"
			case 2:
				s = "meso"
			case 3:
				s = "macro"
			case 4, 5:
				s = "macro"
			}
		case float64:
			return enclaveTierToTaxonomy(int64(x), "")
		case string:
			s = x
		}
	}
	switch strings.ToLower(s) {
	case "1", "micro":
		return ""
	case "2", "meso":
		return "meso"
	case "3", "macro", "4", "workgroup", "5", "enterprise_core":
		return "macro"
	default:
		return ""
	}
}

func buildDatasetURI(doc *TrainingDoc) string {
	p := strings.TrimSpace(doc.Data.Path)
	if p != "" && !strings.EqualFold(p, "qminiwasm/hf-multi") && !strings.EqualFold(p, "qminiwasm/multi") {
		return p
	}
	if len(doc.HuggingFace.ExtraSpecs) > 0 {
		ep := strings.TrimSpace(doc.HuggingFace.ExtraSpecs[0].Path)
		if ep != "" {
			if cfg := strings.TrimSpace(doc.HuggingFace.ExtraSpecs[0].DatasetConfig); cfg != "" {
				return ep + ":" + cfg
			}
			return ep
		}
	}
	if p != "" {
		return p
	}
	return "synthetic"
}

// MergeCheckpointPaths merges [checkpoint] and [tpem] paths (tpem overrides when set).
func MergeCheckpointPaths(doc *TrainingDoc) (load, save, best, latest string) {
	load = strings.TrimSpace(doc.Checkpoint.LoadPath)
	save = strings.TrimSpace(doc.Checkpoint.SavePath)
	best = strings.TrimSpace(doc.Checkpoint.BestPath)
	latest = strings.TrimSpace(doc.Checkpoint.LatestPath)
	if t := strings.TrimSpace(doc.Tpem.LoadPath); t != "" {
		load = t
	}
	if t := strings.TrimSpace(doc.Tpem.SavePath); t != "" {
		save = t
	}
	if t := strings.TrimSpace(doc.Tpem.BestPath); t != "" {
		best = t
	}
	if t := strings.TrimSpace(doc.Tpem.LatestPath); t != "" {
		latest = t
	}
	return load, save, best, latest
}

func resolveOptionalRepoPath(root, user string) (string, error) {
	user = strings.TrimSpace(user)
	if user == "" {
		return "", nil
	}
	return ResolveRepoRelativePath(root, user)
}

func primaryHFDatasetID(doc *TrainingDoc) string {
	p := strings.TrimSpace(doc.Data.Path)
	if p != "" && !strings.EqualFold(p, "qminiwasm/hf-multi") && !strings.EqualFold(p, "qminiwasm/multi") {
		if i := strings.IndexByte(p, ':'); i > 0 {
			return p[:i]
		}
		return p
	}
	if len(doc.HuggingFace.ExtraSpecs) > 0 {
		if ep := strings.TrimSpace(doc.HuggingFace.ExtraSpecs[0].Path); ep != "" {
			return ep
		}
	}
	return ""
}

// BuildHfProto builds HF dataset params from doc (for tests and TrainingTOMLToProto).
func BuildHfProto(doc *TrainingDoc, repoRoot string) (*trainingrpc.HfDatasetParams, error) {
	ds := primaryHFDatasetID(doc)
	if ds == "" {
		return nil, nil
	}
	hf := &trainingrpc.HfDatasetParams{
		DatasetId: ds,
		Split:     "train",
	}
	if doc.HuggingFace.DatasetConfig != nil && strings.TrimSpace(*doc.HuggingFace.DatasetConfig) != "" {
		hf.ConfigName = strings.TrimSpace(*doc.HuggingFace.DatasetConfig)
	}
	if doc.HuggingFace.Split != nil && strings.TrimSpace(*doc.HuggingFace.Split) != "" {
		hf.Split = strings.TrimSpace(*doc.HuggingFace.Split)
	}
	if doc.HuggingFace.DatasetRevision != nil {
		hf.Revision = strings.TrimSpace(*doc.HuggingFace.DatasetRevision)
	}
	if doc.HuggingFace.NumSamples != nil && *doc.HuggingFace.NumSamples > 0 {
		hf.NumSamples = uint32(*doc.HuggingFace.NumSamples)
	}
	if doc.HuggingFace.MeshBlendFraction != nil {
		hf.MeshBlendFraction = *doc.HuggingFace.MeshBlendFraction
	}
	if strings.EqualFold(strings.TrimSpace(doc.Data.Path), "qminiwasm/hf-multi") ||
		strings.EqualFold(strings.TrimSpace(doc.Data.Path), "qminiwasm/multi") {
		if len(doc.HuggingFace.ExtraSpecs) > 0 {
			hf.DatasetId = strings.TrimSpace(doc.HuggingFace.ExtraSpecs[0].Path)
			if cfg := strings.TrimSpace(doc.HuggingFace.ExtraSpecs[0].DatasetConfig); cfg != "" {
				hf.ConfigName = cfg
			}
		}
	}
	if strings.TrimSpace(hf.ConfigName) == "" && strings.TrimSpace(hf.DatasetId) != "" {
		ctx, cancel := context.WithTimeout(context.Background(), 25*time.Second)
		defer cancel()
		if cfg, err := ResolveHFDefaultConfigName(ctx, hf.DatasetId); err == nil && cfg != "" {
			hf.ConfigName = cfg
		}
	}
	_ = repoRoot
	return hf, nil
}

func buildCascadeProto(doc *TrainingDoc, repoRoot string) (*trainingrpc.CascadeCurriculumLoopParams, bool, error) {
	c := &doc.CascadeCurriculumLoop
	if c.Enabled == nil || !*c.Enabled {
		return nil, false, nil
	}
	out := &trainingrpc.CascadeCurriculumLoopParams{
		Enabled:               true,
		MaxHealRounds:         3,
		PtqtpNumPlanes:        2,
		GateTargetValMse:      0,
		GateMaxTpemMib:        0,
		RunTaxonomyLinter:     false,
		HealLearningRateScale: 0.5,
		TeacherEpochFraction:  0.5,
		HealEpochsPerRound:    2,
		MaxCurriculumCycles:   1,
	}
	if c.MaxHealRounds != nil && *c.MaxHealRounds > 0 {
		out.MaxHealRounds = uint32(*c.MaxHealRounds)
	}
	if c.PtqtpNumPlanes != nil && *c.PtqtpNumPlanes > 0 {
		out.PtqtpNumPlanes = uint32(*c.PtqtpNumPlanes)
	}
	if c.GateTargetValMse != nil {
		out.GateTargetValMse = *c.GateTargetValMse
	}
	if c.GateMaxTpemMib != nil {
		out.GateMaxTpemMib = *c.GateMaxTpemMib
	}
	if c.RunTaxonomyLinter != nil {
		out.RunTaxonomyLinter = *c.RunTaxonomyLinter
	}
	if c.HealLearningRateScale != nil && *c.HealLearningRateScale > 0 {
		out.HealLearningRateScale = *c.HealLearningRateScale
	}
	if c.TeacherEpochFraction != nil && *c.TeacherEpochFraction > 0 {
		out.TeacherEpochFraction = *c.TeacherEpochFraction
	}
	if c.HealEpochsPerRound != nil && *c.HealEpochsPerRound > 0 {
		out.HealEpochsPerRound = uint32(*c.HealEpochsPerRound)
	}
	if c.MaxCurriculumCycles != nil && *c.MaxCurriculumCycles > 0 {
		out.MaxCurriculumCycles = uint32(*c.MaxCurriculumCycles)
	}
	tp := strings.TrimSpace(c.TeacherCheckpointPath)
	if tp != "" {
		abs, err := resolveOptionalRepoPath(repoRoot, tp)
		if err != nil {
			return nil, false, fmt.Errorf("cascade teacher_checkpoint_path: %w", err)
		}
		out.TeacherCheckpointPath = abs
	}
	return out, true, nil
}

// TrainingTomlCascadeRequiresNativeGRPC reports whether the TOML enables the automated cascade loop.
func TrainingTomlCascadeRequiresNativeGRPC(absConfigPath string) (bool, error) {
	raw, err := os.ReadFile(absConfigPath)
	if err != nil {
		return false, err
	}
	var doc TrainingDoc
	if err := toml.Unmarshal(raw, &doc); err != nil {
		return false, fmt.Errorf("parse training toml: %w", err)
	}
	return doc.CascadeCurriculumLoop.Enabled != nil && *doc.CascadeCurriculumLoop.Enabled, nil
}

// TrainingTOMLToProto maps a training TOML file to trainingrpc.TrainingConfig.
func TrainingTOMLToProto(absConfigPath, runID, repoRoot string) (*trainingrpc.TrainingConfig, error) {
	raw, err := os.ReadFile(absConfigPath)
	if err != nil {
		return nil, err
	}
	var doc TrainingDoc
	if err := toml.Unmarshal(raw, &doc); err != nil {
		return nil, fmt.Errorf("parse training toml: %w", err)
	}
	tierStr := ""
	if doc.Enclave.EnclaveTier != nil {
		tierStr = fmt.Sprint(doc.Enclave.EnclaveTier)
	}

	loadPath, savePath, bestPath, latestPath := MergeCheckpointPaths(&doc)
	root := strings.TrimSpace(repoRoot)
	if root == "" {
		return nil, errors.New("repo root is required to resolve checkpoint paths")
	}

	absSave, err := resolveOptionalRepoPath(root, savePath)
	if err != nil {
		return nil, fmt.Errorf("checkpoint save_path: %w", err)
	}
	absBest, err := resolveOptionalRepoPath(root, bestPath)
	if err != nil {
		return nil, fmt.Errorf("checkpoint best_path: %w", err)
	}
	absLatest, err := resolveOptionalRepoPath(root, latestPath)
	if err != nil {
		return nil, fmt.Errorf("checkpoint latest_path: %w", err)
	}
	cfg := &trainingrpc.TrainingConfig{
		RunId:                runID,
		DatasetUri:           buildDatasetURI(&doc),
		ModelUri:             strings.TrimSpace(loadPath),
		Epochs:               u32Or(doc.Training.Epochs, 1),
		BatchSize:            u32Or(doc.Training.BatchSize, 64),
		MicroBatchSize:       u32Or(doc.Training.MicroBatch, 16),
		PrefetchDepth:        u32Or(doc.Training.PrefetchDepth, 4),
		ComputeSlots:         u32Or(doc.Training.ComputeSlots, 2),
		WorkerThreads:        u32Or(doc.Training.WorkerThreads, 4),
		LearningRate:         f64Or(doc.Training.LearningRate, 1e-3),
		Seed:                 u64Seed(doc.Training.Seed, 42),
		TaxonomyTier:         enclaveTierToTaxonomy(doc.Enclave.EnclaveTier, tierStr),
		PrecisionPolicy:      "fp32",
		CheckpointSavePath:   absSave,
		CheckpointBestPath:   absBest,
		CheckpointLatestPath: absLatest,
	}
	if doc.Adapter.UseTsignTernary != nil && *doc.Adapter.UseTsignTernary {
		cfg.PrecisionPolicy = "ternary"
	}
	if doc.Model.DModel != nil && *doc.Model.DModel > 0 {
		cfg.DModel = uint32(*doc.Model.DModel)
	}
	if doc.Model.IoDModel != nil && *doc.Model.IoDModel > 0 {
		cfg.IoDModel = uint32(*doc.Model.IoDModel)
	}
	if doc.Model.NumTernaryBlocks != nil && *doc.Model.NumTernaryBlocks > 0 {
		cfg.NumTernaryBlocks = uint32(*doc.Model.NumTernaryBlocks)
	}
	if doc.Model.NativeBlochSeqLen != nil && *doc.Model.NativeBlochSeqLen > 0 {
		cfg.NativeBlochSeqLen = uint32(*doc.Model.NativeBlochSeqLen)
	}
	if doc.Model.NativeBlochNumHeads != nil && *doc.Model.NativeBlochNumHeads > 0 {
		cfg.NativeBlochNumHeads = uint32(*doc.Model.NativeBlochNumHeads)
	}

	src := strings.ToLower(strings.TrimSpace(doc.Data.Source))
	if src == "hf_tabular" || src == "hf" {
		hfProto, err := BuildHfProto(&doc, root)
		if err != nil {
			return nil, err
		}
		if hfProto != nil && hfProto.DatasetId != "" {
			cfg.Hf = hfProto
		}
	}

	cascadeProto, cascadeOn, err := buildCascadeProto(&doc, root)
	if err != nil {
		return nil, err
	}
	if cascadeOn {
		cfg.CascadeLoop = cascadeProto
		cfg.UseNativeEngineOnly = true
		autoB := InferNativeCascadeBatchSize(cfg.GetDModel(), cfg.GetIoDModel(), cfg.GetNumTernaryBlocks())
		cfg.BatchSize = autoB
		cfg.MicroBatchSize = autoB
	}

	if err := applyUnifiedTrainingMatrixTOML(&doc, cfg); err != nil {
		return nil, err
	}
	return cfg, nil
}
