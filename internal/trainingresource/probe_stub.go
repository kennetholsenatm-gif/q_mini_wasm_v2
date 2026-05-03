//go:build !windows && !linux && !darwin

package trainingresource

import "runtime"

// ProbeHost reports logical CPUs only (physical RAM unavailable on this GOOS).
func ProbeHost() HostResources {
	return HostResources{LogicalCPUs: runtime.NumCPU(), TotalRAMBytes: 0, MemoryLoadPercent: 101}
}
