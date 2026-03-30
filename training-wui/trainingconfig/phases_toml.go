package trainingconfig

import (
	"fmt"
	"strings"

	"google.golang.org/protobuf/proto"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingrpc"
)

// trainingPhaseToml mirrors [[training_phases]] rows (Unified Training Matrix).
type trainingPhaseToml struct {
	Name                   string   `toml:"name"`
	Epochs                 int64    `toml:"epochs"`
	Supervised             *bool    `toml:"supervised"`
	CascadeRl              *bool    `toml:"cascade_rl"`
	FreezeModelBackbone    *bool    `toml:"freeze_model_backbone"`
	RouterOnly             *bool    `toml:"router_only"`
	CascadePolicyOptimizer string   `toml:"cascade_policy_optimizer"`
	CispoClipEpsilon       *float64 `toml:"cispo_clip_epsilon"`
	CascadeMopdLambda      *float64 `toml:"cascade_mopd_lambda"`
	TequilaDeadzone        *float64 `toml:"tequila_deadzone"`
	FreezeTernaryExperts   *bool    `toml:"freeze_ternary_experts"`
}

func boolOrTrue(p *bool) bool {
	if p == nil {
		return true
	}
	return *p
}

func boolOrFalse(p *bool) bool {
	if p == nil {
		return false
	}
	return *p
}

func normalizeCascadePolicy(s string) (string, error) {
	v := strings.ToLower(strings.TrimSpace(s))
	if v == "" {
		return "grpo", nil
	}
	if v == "grpo" || v == "cispo" {
		return v, nil
	}
	return "", fmt.Errorf("cascade policy_optimizer: want grpo or cispo, got %q", s)
}

// applyUnifiedTrainingMatrixTOML fills cfg.training_phases, epoch sum, cascade policy, and attention_backend.
func applyUnifiedTrainingMatrixTOML(doc *TrainingDoc, cfg *trainingrpc.TrainingConfig) error {
	if doc.Model.AttentionBackend != nil {
		cfg.AttentionBackend = strings.TrimSpace(*doc.Model.AttentionBackend)
	}
	rootPol := "grpo"
	var err error
	if doc.Cascade.PolicyOptimizer != nil {
		rootPol, err = normalizeCascadePolicy(*doc.Cascade.PolicyOptimizer)
		if err != nil {
			return err
		}
	}
	cfg.CascadePolicyOptimizer = rootPol
	if doc.Cascade.CispoClipEpsilon != nil {
		cfg.CispoClipEpsilon = *doc.Cascade.CispoClipEpsilon
	} else {
		cfg.CispoClipEpsilon = 0.2
	}
	cfg.CascadeRlGroupSize = 4
	if doc.Cascade.GroupSize != nil && *doc.Cascade.GroupSize >= 1 {
		if *doc.Cascade.GroupSize > int64(^uint32(0)) {
			return fmt.Errorf("[cascade] group_size overflows uint32")
		}
		cfg.CascadeRlGroupSize = uint32(*doc.Cascade.GroupSize)
	}

	if len(doc.TrainingPhases) == 0 {
		return nil
	}

	var sum uint64
	out := make([]*trainingrpc.TrainingPhaseParams, 0, len(doc.TrainingPhases))
	for i, row := range doc.TrainingPhases {
		nm := strings.TrimSpace(row.Name)
		if nm == "" {
			return fmt.Errorf("training_phases[%d]: name is required", i)
		}
		if row.Epochs < 1 {
			return fmt.Errorf("training_phases[%q]: epochs must be >= 1", nm)
		}
		sum += uint64(row.Epochs)
		po := strings.TrimSpace(row.CascadePolicyOptimizer)
		if po != "" {
			if _, err := normalizeCascadePolicy(po); err != nil {
				return fmt.Errorf("training_phases[%q]: %w", nm, err)
			}
		}
		pp := &trainingrpc.TrainingPhaseParams{
			Name:                   nm,
			Epochs:                 uint32(row.Epochs),
			Supervised:             boolOrTrue(row.Supervised),
			CascadeRl:              boolOrTrue(row.CascadeRl),
			FreezeModelBackbone:    boolOrFalse(row.FreezeModelBackbone),
			RouterOnly:             boolOrFalse(row.RouterOnly),
			CascadePolicyOptimizer: strings.ToLower(po),
			FreezeTernaryExperts:   boolOrFalse(row.FreezeTernaryExperts),
		}
		if row.CispoClipEpsilon != nil {
			pp.CispoClipEpsilon = proto.Float64(*row.CispoClipEpsilon)
		}
		if row.CascadeMopdLambda != nil {
			pp.CascadeMopdLambda = proto.Float64(*row.CascadeMopdLambda)
		}
		if row.TequilaDeadzone != nil {
			pp.TequilaDeadzone = proto.Float64(*row.TequilaDeadzone)
		}
		out = append(out, pp)
	}
	if sum > uint64(^uint32(0)) {
		return fmt.Errorf("training_phases: sum of epochs overflows uint32")
	}
	cfg.Epochs = uint32(sum)
	cfg.TrainingPhases = out
	return nil
}
