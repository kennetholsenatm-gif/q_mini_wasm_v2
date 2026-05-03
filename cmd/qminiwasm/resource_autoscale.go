package main

import (
	"fmt"

	"github.com/q_mini_wasm_v2/gateway/internal/trainingresource"
)

// scaledTrainingRuntime holds integers passed to Training_InitSession after optional resource autoscale.
type scaledTrainingRuntime struct {
	Buffer                  BufferTuning
	ParallelBatches         int
	AcquisitionThreads      int
	PerturbationThreads     int
	CheckpointAsyncQueueMax int
	Gf3FFBatchedWeightMib   int
	TargetRoutesPerBatch    int
}

func parallelBatchesFromConfig(cfg *Config) int {
	p := cfg.GetInt("training.parallel_batches")
	if p < 1 {
		return 1
	}
	return p
}

// applyResourceAutoscale treats TOML as the ceiling for a reference machine; smaller hosts scale down proportionally.
func applyResourceAutoscale(cfg *Config, base BufferTuning) (out scaledTrainingRuntime, probe trainingresource.HostResources, rs trainingresource.ScaleFactors, logLine string) {
	probe = trainingresource.ProbeHost()
	out = scaledTrainingRuntime{
		Buffer:                  base,
		ParallelBatches:         parallelBatchesFromConfig(cfg),
		AcquisitionThreads:      cfg.GetInt("training.acquisition_threads"),
		PerturbationThreads:     cfg.GetInt("training.perturbation_threads"),
		CheckpointAsyncQueueMax: cfg.GetInt("training.checkpoint_async_queue_max"),
		Gf3FFBatchedWeightMib:   cfg.GetInt("training.gf3_ff_batched_weight_mib"),
		TargetRoutesPerBatch:    cfg.GetInt("training.target_routes_per_batch"),
	}

	if !cfg.GetBoolDefault("training.resource_autoscale", false) {
		rs = trainingresource.ScaleFactors{Scale: 1, CPUFactor: 1, RAMFactor: 1, VRAMFactor: 1, Headroom: 1, VRAMHeadroom: 1}
		return out, probe, rs, ""
	}

	includeVRAM := cfg.GetBoolDefault("training.resource_autoscale_include_vram", true)
	if includeVRAM {
		gpuIdx := int32(cfg.GetInt("training.sycl_gpu_device_index"))
		gmem, isGPU, _, rc := probeSyclDeviceVRAM(gpuIdx)
		if rc == 0 && gmem > 0 {
			probe.GpuGlobalMemBytes = gmem
			probe.GpuIsGPU = isGPU
		}
	}

	refCPU := float64(cfg.GetInt("training.resource_autoscale_ref_logical_cpus"))
	refRAM := float64(cfg.GetInt("training.resource_autoscale_ref_ram_gib"))
	refVRAM := float64(cfg.GetInt("training.resource_autoscale_ref_vram_gib"))
	hr := cfg.GetFloat("training.resource_autoscale_ram_headroom")
	vramHR := cfg.GetFloat("training.resource_autoscale_vram_headroom")
	rs = trainingresource.ComputeScale(probe, refCPU, refRAM, refVRAM, hr, vramHR, includeVRAM)
	s := rs.Scale
	minPB := cfg.GetInt("training.resource_autoscale_min_parallel_batches")
	if minPB < 1 {
		minPB = 1
	}

	pb := parallelBatchesFromConfig(cfg)
	out.ParallelBatches = trainingresource.ScaleIntFloor(pb, s, minPB)

	out.Buffer = BufferTuning{
		Profile:              base.Profile,
		PrefillTarget:        trainingresource.ScaleIntFloor(base.PrefillTarget, s, 1),
		PrefillTimeoutMs:     base.PrefillTimeoutMs,
		PrefillPollMs:        base.PrefillPollMs,
		AcqQueueCap:          trainingresource.ScaleIntFloor(base.AcqQueueCap, s, 1),
		RawQueueCap:          trainingresource.ScaleIntFloor(base.RawQueueCap, s, 1),
		TrainQueueCap:        trainingresource.ScaleIntFloor(base.TrainQueueCap, s, 1),
		DirectoryMaxLines:    trainingresource.ScaleUint64Floor(base.DirectoryMaxLines, s, 1),
		MaxJsonlLocalSamples: trainingresource.ScaleUint64Floor(base.MaxJsonlLocalSamples, s, 1),
		MinTextLength:        base.MinTextLength,
		MaxTextLength:        base.MaxTextLength,
	}

	if cfg.GetBoolDefault("training.resource_autoscale_scale_threads", true) {
		out.AcquisitionThreads = trainingresource.ScaleIntFloor(cfg.GetInt("training.acquisition_threads"), s, 1)
		out.PerturbationThreads = trainingresource.ScaleIntFloor(cfg.GetInt("training.perturbation_threads"), s, 1)
		out.CheckpointAsyncQueueMax = trainingresource.ScaleIntFloor(cfg.GetInt("training.checkpoint_async_queue_max"), s, 1)
	}

	if cfg.GetBoolDefault("training.resource_autoscale_scale_gf3_ff_mib", true) {
		out.Gf3FFBatchedWeightMib = trainingresource.ScaleIntFloor(cfg.GetInt("training.gf3_ff_batched_weight_mib"), s, 8)
	}

	if cfg.IsSet("training.target_routes_per_batch") && cfg.GetInt("training.target_routes_per_batch") > 0 {
		out.TargetRoutesPerBatch = trainingresource.ScaleIntFloor(cfg.GetInt("training.target_routes_per_batch"), s, 1)
	}

	var ramNote string
	if probe.TotalRAMBytes == 0 {
		ramNote = "ram=? (cpu-only for ram_f)"
	} else {
		ramNote = fmt.Sprintf("eff_ram=%.1fGiB×ram_h", rs.EffectiveGiB)
	}
	var vramNote string
	if !includeVRAM {
		vramNote = "vram=off"
	} else if probe.GpuGlobalMemBytes == 0 {
		vramNote = "vram=? (SYCL probe miss)"
	} else {
		vramNote = fmt.Sprintf("eff_vram=%.1fGiB×vram_h", rs.EffectiveVRAMGiB)
	}

	logLine = fmt.Sprintf(
		"[Training] resource_autoscale: %s ref=%.0fcpus/%.0fGiB_ram/%.0fGiB_vram scale=%.3f (cpu_f=%.3f ram_f=%.3f vram_f=%.3f %s; %s) parallel_batches %d→%d prefill %d→%d acq %d→%d raw %d→%d train %d→%d acq_thr %d→%d pert_thr %d→%d ckpt_q %d→%d gf3_ff_mib %d→%d target_routes %d→%d",
		trainingresource.FormatHostSummary(probe),
		rs.RefCPUs, rs.RefRAMGiB, rs.RefVRAMGiB,
		rs.Scale, rs.CPUFactor, rs.RAMFactor, rs.VRAMFactor,
		ramNote, vramNote,
		pb, out.ParallelBatches,
		base.PrefillTarget, out.Buffer.PrefillTarget,
		base.AcqQueueCap, out.Buffer.AcqQueueCap,
		base.RawQueueCap, out.Buffer.RawQueueCap,
		base.TrainQueueCap, out.Buffer.TrainQueueCap,
		cfg.GetInt("training.acquisition_threads"), out.AcquisitionThreads,
		cfg.GetInt("training.perturbation_threads"), out.PerturbationThreads,
		cfg.GetInt("training.checkpoint_async_queue_max"), out.CheckpointAsyncQueueMax,
		cfg.GetInt("training.gf3_ff_batched_weight_mib"), out.Gf3FFBatchedWeightMib,
		cfg.GetInt("training.target_routes_per_batch"), out.TargetRoutesPerBatch,
	)
	return out, probe, rs, logLine
}
