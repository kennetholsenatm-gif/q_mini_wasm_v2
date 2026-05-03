package trainingresource

import "fmt"

// HostResources is a small snapshot for proportional training autoscale (RAM/GPU bytes may be 0 = unknown).
type HostResources struct {
	LogicalCPUs       int
	TotalRAMBytes     uint64
	GpuGlobalMemBytes uint64
	GpuIsGPU          bool
	// MemoryLoadPercent is approximate system memory pressure (Windows GlobalMemoryStatusEx). 101 = unknown.
	MemoryLoadPercent uint32
}

func FormatHostSummary(r HostResources) string {
	s := fmt.Sprintf("cpus=%d", r.LogicalCPUs)
	if r.TotalRAMBytes == 0 {
		s += " ram=?GiB"
	} else {
		gib := float64(r.TotalRAMBytes) / (1024 * 1024 * 1024)
		s += fmt.Sprintf(" ram=%.1fGiB", gib)
	}
	if r.GpuGlobalMemBytes > 0 {
		gvg := float64(r.GpuGlobalMemBytes) / (1024 * 1024 * 1024)
		kind := "accel"
		if r.GpuIsGPU {
			kind = "gpu"
		}
		s += fmt.Sprintf(" sycl_%s_vram=%.1fGiB", kind, gvg)
	}
	if r.MemoryLoadPercent <= 100 {
		s += fmt.Sprintf(" mem_load=%d%%", r.MemoryLoadPercent)
	}
	return s
}
