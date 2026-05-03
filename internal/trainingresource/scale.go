package trainingresource

import "math"

// ScaleFactors is the multiplicative scale plus diagnostic ratios (TOML is the ceiling; scale ≤ 1).
type ScaleFactors struct {
	Scale            float64
	CPUFactor        float64
	RAMFactor        float64
	VRAMFactor       float64
	RefCPUs          float64
	RefRAMGiB        float64
	RefVRAMGiB       float64
	Headroom         float64
	VRAMHeadroom     float64
	EffectiveGiB     float64
	EffectiveVRAMGiB float64
}

// ComputeScale returns min(1, cpuRatio, ramRatio [, vramRatio]) comparing this host to reference hardware.
// If TotalRAMBytes == 0, RAM is unconstrained (RAMFactor = 1).
// If constrainVRAM is false or GpuGlobalMemBytes == 0 or refVRAMGiB <= 0, VRAM is unconstrained (VRAMFactor = 1).
func ComputeScale(
	r HostResources,
	refLogicalCPUs, refRAMGiB, refVRAMGiB float64,
	ramHeadroom, vramHeadroom float64,
	constrainVRAM bool,
) ScaleFactors {
	if refLogicalCPUs <= 0 {
		refLogicalCPUs = 32
	}
	if refRAMGiB <= 0 {
		refRAMGiB = 128
	}
	if refVRAMGiB <= 0 {
		refVRAMGiB = 32
	}
	if ramHeadroom <= 0 || ramHeadroom > 1 {
		ramHeadroom = 0.9
	}
	if vramHeadroom <= 0 || vramHeadroom > 1 {
		vramHeadroom = 0.85
	}

	cpuF := float64(max(1, r.LogicalCPUs)) / refLogicalCPUs
	if cpuF > 1 {
		cpuF = 1
	}

	ramF := 1.0
	var effGiB float64
	if r.TotalRAMBytes > 0 {
		effGiB = (float64(r.TotalRAMBytes) / (1024 * 1024 * 1024)) * ramHeadroom
		ramF = effGiB / refRAMGiB
		if ramF > 1 {
			ramF = 1
		}
	}

	vramF := 1.0
	var effVRAMGiB float64
	if constrainVRAM && r.GpuGlobalMemBytes > 0 && refVRAMGiB > 0 {
		effVRAMGiB = (float64(r.GpuGlobalMemBytes) / (1024 * 1024 * 1024)) * vramHeadroom
		vramF = effVRAMGiB / refVRAMGiB
		if vramF > 1 {
			vramF = 1
		}
	}

	scale := math.Min(math.Min(cpuF, ramF), vramF)
	if scale > 1 {
		scale = 1
	}
	if scale < 0 {
		scale = 0
	}

	return ScaleFactors{
		Scale:            scale,
		CPUFactor:        cpuF,
		RAMFactor:        ramF,
		VRAMFactor:       vramF,
		RefCPUs:          refLogicalCPUs,
		RefRAMGiB:        refRAMGiB,
		RefVRAMGiB:       refVRAMGiB,
		Headroom:         ramHeadroom,
		VRAMHeadroom:     vramHeadroom,
		EffectiveGiB:     effGiB,
		EffectiveVRAMGiB: effVRAMGiB,
	}
}

// ScaleIntFloor rounds v*scale toward nearest integer, clamps to [minV, v].
func ScaleIntFloor(v int, scale float64, minV int) int {
	if v <= 0 {
		return minV
	}
	if scale <= 0 {
		return minV
	}
	x := int(math.Round(float64(v) * scale))
	if x < minV {
		x = minV
	}
	if x > v {
		x = v
	}
	return x
}

// ScaleUint64Floor is like ScaleIntFloor for unsigned caps.
func ScaleUint64Floor(v uint64, scale float64, minV uint64) uint64 {
	if v <= 0 {
		return minV
	}
	if scale <= 0 {
		return minV
	}
	x := uint64(math.Round(float64(v) * scale))
	if x < minV {
		x = minV
	}
	if x > v {
		x = v
	}
	return x
}
