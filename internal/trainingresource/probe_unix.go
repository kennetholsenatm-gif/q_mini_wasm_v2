//go:build linux || darwin

package trainingresource

import (
	"runtime"

	"golang.org/x/sys/unix"
)

// ProbeHost reads logical CPUs and installed physical RAM (best-effort).
func ProbeHost() HostResources {
	cpus := runtime.NumCPU()
	pages, err := unix.Sysconf(unix._SC_PHYS_PAGES)
	if err != nil {
		return HostResources{LogicalCPUs: cpus, TotalRAMBytes: 0}
	}
	psize, err := unix.Sysconf(unix._SC_PAGE_SIZE)
	if err != nil {
		return HostResources{LogicalCPUs: cpus, TotalRAMBytes: 0}
	}
	total := uint64(pages) * uint64(psize)
	return HostResources{LogicalCPUs: cpus, TotalRAMBytes: total, MemoryLoadPercent: 101}
}
