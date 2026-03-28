package main

import (
	"errors"
	"fmt"
	"os"
	"strings"

	toml "github.com/pelletier/go-toml/v2"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingrpc"
)

// trainingDoc is a minimal TOML shape for mapping Python training configs to the C++ gRPC TrainingConfig.
// Many keys are ignored until the engine loads full TOML server-side.
type trainingDoc struct {
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
		DatasetConfig *string      `toml:"dataset_config"`
		ExtraSpecs    []hfExtraToml `toml:"extra_specs"`
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
		DModel            *int64 `toml:"d_model"`
		IoDModel          *int64 `toml:"io_d_model"`
		NumTernaryBlocks  *int64 `toml:"num_ternary_blocks"`
	} `toml:"model"`
}

type hfExtraToml struct {
	Path           string `toml:"path"`
	DatasetConfig  string `toml:"dataset_config"`
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

// enclaveTierToTaxonomy maps TOML [enclave].enclave_tier to strings understood by C++ parse_taxonomy_tier.
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
		return "" // edge default in C++
	case "2", "meso":
		return "meso"
	case "3", "macro", "4", "workgroup", "5", "enterprise_core":
		return "macro"
	default:
		return ""
	}
}

func buildDatasetURI(doc *trainingDoc) string {
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

func mergeCheckpointPaths(doc *trainingDoc) (load, save, best, latest string) {
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
	return resolveRepoRelativePath(root, user)
}

// TrainingTOMLToProto maps a training TOML file to qminiwasm.trainingrpc.TrainingConfig (plan B — not full TOML parity).
// repoRoot is used to resolve checkpoint paths to absolute paths for the C++ engine.
func TrainingTOMLToProto(absConfigPath, runID, repoRoot string) (*trainingrpc.TrainingConfig, error) {
	raw, err := os.ReadFile(absConfigPath)
	if err != nil {
		return nil, err
	}
	var doc trainingDoc
	if err := toml.Unmarshal(raw, &doc); err != nil {
		return nil, fmt.Errorf("parse training toml: %w", err)
	}
	tierStr := ""
	if doc.Enclave.EnclaveTier != nil {
		tierStr = fmt.Sprint(doc.Enclave.EnclaveTier)
	}

	loadPath, savePath, bestPath, latestPath := mergeCheckpointPaths(&doc)
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
		RunId:             runID,
		DatasetUri:        buildDatasetURI(&doc),
		ModelUri:          strings.TrimSpace(loadPath),
		Epochs:            u32Or(doc.Training.Epochs, 1),
		BatchSize:         u32Or(doc.Training.BatchSize, 64),
		MicroBatchSize:    u32Or(doc.Training.MicroBatch, 16),
		PrefetchDepth:     u32Or(doc.Training.PrefetchDepth, 4),
		ComputeSlots:      u32Or(doc.Training.ComputeSlots, 2),
		WorkerThreads:     u32Or(doc.Training.WorkerThreads, 4),
		LearningRate:      f64Or(doc.Training.LearningRate, 1e-3),
		Seed:              u64Seed(doc.Training.Seed, 42),
		TaxonomyTier:      enclaveTierToTaxonomy(doc.Enclave.EnclaveTier, tierStr),
		PrecisionPolicy:        "fp32",
		CheckpointSavePath:     absSave,
		CheckpointBestPath:     absBest,
		CheckpointLatestPath:   absLatest,
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
	return cfg, nil
}
