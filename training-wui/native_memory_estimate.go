package main

import (
	"fmt"
	"strings"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingrpc"
)

// computeNativeColdStartMemoryEstimateMap matches cpp/training/src/grpc/training_engine_service.cpp
// FillNativeColdStartMemoryEstimate (informational only).
func computeNativeColdStartMemoryEstimateMap(cfg *trainingrpc.TrainingConfig) map[string]any {
	if cfg == nil {
		return nil
	}
	d := uint64(cfg.GetDModel())
	if d == 0 {
		d = 4096
	}
	io := uint64(cfg.GetIoDModel())
	if io == 0 {
		io = d
	}
	nb := uint64(cfg.GetNumTernaryBlocks())
	if nb == 0 {
		nb = 1
	}
	batch := uint64(cfg.GetBatchSize())
	if batch == 0 {
		batch = 1
	}
	checkpoint := cfg.GetModelUri() != ""

	var paramFloats uint64
	if io != d {
		paramFloats += 2*io*d + d + io
	}
	paramFloats += nb * (d*d + 2*d)

	paramBytes := paramFloats * 4
	adamBytes := paramBytes * 2
	actBytes := batch * (io + d*(nb+2)) * 4 * 2

	summary := "Dense FP32 native TPEM cold-start: param floats = (io!=d ? 2*io*d+d+io : 0) + Nb*(d^2+2d); " +
		"param_bytes=4*floats; adam_bytes~=2*param_bytes; scratch_hint=batch*(io+d*(Nb+2))*4*2 (order-of-magnitude)."
	if checkpoint {
		summary += " model_uri set: on-disk interchange defines actual tensors; this estimate uses proto geometry for planning."
	}

	return map[string]any{
		"parameter_bytes_fp32":              paramBytes,
		"adam_state_bytes_fp32":             adamBytes,
		"activation_scratch_bytes_hint":     actBytes,
		"formula_summary":                   summary,
		"effective_d_model":                 uint32(d),
		"effective_io_d_model":              uint32(io),
		"effective_num_ternary_blocks":      uint32(nb),
		"batch_size":                        uint32(batch),
		"model_uri_checkpoint_load":         checkpoint,
		"parameter_gib":                     float64(paramBytes) / (1024 * 1024 * 1024),
		"adam_state_gib":                    float64(adamBytes) / (1024 * 1024 * 1024),
		"activation_scratch_hint_gib":       float64(actBytes) / (1024 * 1024 * 1024),
		"total_params_plus_adam_bytes":      paramBytes + adamBytes,
		"total_params_plus_adam_gib":        float64(paramBytes+adamBytes) / (1024 * 1024 * 1024),
		"total_including_scratch_hint_gib":  float64(paramBytes+adamBytes+actBytes) / (1024 * 1024 * 1024),
		"total_including_scratch_hint_bytes": paramBytes + adamBytes + actBytes,
	}
}

func nativeMemoryEstimateProtoToMap(est *trainingrpc.NativeColdStartMemoryEstimate) map[string]any {
	if est == nil {
		return nil
	}
	p := est.GetParameterBytesFp32()
	a := est.GetAdamStateBytesFp32()
	s := est.GetActivationScratchBytesHint()
	return map[string]any{
		"parameter_bytes_fp32":               p,
		"adam_state_bytes_fp32":              a,
		"activation_scratch_bytes_hint":      s,
		"formula_summary":                    est.GetFormulaSummary(),
		"effective_d_model":                  est.GetEffectiveDModel(),
		"effective_io_d_model":               est.GetEffectiveIoDModel(),
		"effective_num_ternary_blocks":       est.GetEffectiveNumTernaryBlocks(),
		"batch_size":                         est.GetBatchSize(),
		"model_uri_checkpoint_load":          est.GetModelUriCheckpointLoad(),
		"parameter_gib":                      float64(p) / (1024 * 1024 * 1024),
		"adam_state_gib":                     float64(a) / (1024 * 1024 * 1024),
		"activation_scratch_hint_gib":      float64(s) / (1024 * 1024 * 1024),
		"total_params_plus_adam_bytes":       p + a,
		"total_params_plus_adam_gib":         float64(p+a) / (1024 * 1024 * 1024),
		"total_including_scratch_hint_bytes": p + a + s,
		"total_including_scratch_hint_gib":   float64(p+a+s) / (1024 * 1024 * 1024),
	}
}

func formatStartTrainingMemoryLog(est *trainingrpc.NativeColdStartMemoryEstimate) string {
	if est == nil {
		return ""
	}
	m := nativeMemoryEstimateProtoToMap(est)
	var b strings.Builder
	b.WriteString(fmt.Sprintf(
		"[native:gRPC] cold-start estimate: params %.4f GiB · adam≈%.4f GiB · scratch≈%.4f GiB · params+adam %.4f GiB · d=%d io=%d nb=%d batch=%d\n",
		m["parameter_gib"],
		m["adam_state_gib"],
		m["activation_scratch_hint_gib"],
		m["total_params_plus_adam_gib"],
		est.GetEffectiveDModel(),
		est.GetEffectiveIoDModel(),
		est.GetEffectiveNumTernaryBlocks(),
		est.GetBatchSize(),
	))
	if est.GetModelUriCheckpointLoad() {
		b.WriteString("[native:gRPC] model_uri set: on-disk tensors may differ from this geometry-based estimate\n")
	}
	return b.String()
}
