package trainingresource

import (
	"math"
	"testing"
)

func TestComputeScale_balanced(t *testing.T) {
	rs := ComputeScale(HostResources{
		LogicalCPUs:   16,
		TotalRAMBytes: 64 * 1024 * 1024 * 1024,
	}, 32, 128, 32, 1.0, 1.0, false)
	if math.Abs(rs.Scale-0.5) > 1e-9 {
		t.Fatalf("scale=%v want 0.5", rs.Scale)
	}
	if math.Abs(rs.VRAMFactor-1.0) > 1e-9 {
		t.Fatalf("vram_f=%v want 1", rs.VRAMFactor)
	}
}

func TestComputeScale_vramBinding(t *testing.T) {
	// Huge CPU/RAM vs reference, tiny VRAM vs 32 GiB ref → scale follows VRAM.
	rs := ComputeScale(HostResources{
		LogicalCPUs:       128,
		TotalRAMBytes:     512 * 1024 * 1024 * 1024,
		GpuGlobalMemBytes: 8 * 1024 * 1024 * 1024,
		GpuIsGPU:          true,
	}, 32, 128, 32, 1.0, 1.0, true)
	// eff_vram = 8, ref = 32 → vramF = 0.25; cpu and ram already 1
	if math.Abs(rs.VRAMFactor-0.25) > 1e-9 {
		t.Fatalf("vram_f=%v want 0.25", rs.VRAMFactor)
	}
	if math.Abs(rs.Scale-0.25) > 1e-9 {
		t.Fatalf("scale=%v want 0.25", rs.Scale)
	}
}

func TestComputeScale_ramUnknownUsesCPUOnly(t *testing.T) {
	rs := ComputeScale(HostResources{LogicalCPUs: 8, TotalRAMBytes: 0}, 32, 128, 32, 0.9, 0.85, true)
	if math.Abs(rs.Scale-0.25) > 1e-9 {
		t.Fatalf("scale=%v want 0.25 (cpu-only)", rs.Scale)
	}
	if math.Abs(rs.RAMFactor-1.0) > 1e-9 {
		t.Fatalf("ram_f=%v want 1", rs.RAMFactor)
	}
}

func TestScaleIntFloor(t *testing.T) {
	if ScaleIntFloor(32, 0.5, 1) != 16 {
		t.Fatal()
	}
	if ScaleIntFloor(32, 1.0, 1) != 32 {
		t.Fatal()
	}
	if ScaleIntFloor(5, 0.1, 2) != 2 {
		t.Fatal()
	}
}
