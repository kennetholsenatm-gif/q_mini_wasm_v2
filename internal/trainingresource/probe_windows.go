//go:build windows

package trainingresource

import (
	"runtime"
	"syscall"
	"unsafe"
)

var (
	modkernel32              = syscall.NewLazyDLL("kernel32.dll")
	procGlobalMemoryStatusEx = modkernel32.NewProc("GlobalMemoryStatusEx")
)

type memoryStatusEx struct {
	Length               uint32
	MemoryLoad           uint32
	TotalPhys            uint64
	AvailPhys            uint64
	TotalPageFile        uint64
	AvailPageFile        uint64
	TotalVirtual         uint64
	AvailVirtual         uint64
	AvailExtendedVirtual uint64
}

// ProbeHost reads logical CPUs and installed physical RAM (best-effort).
func ProbeHost() HostResources {
	cpus := runtime.NumCPU()
	var ms memoryStatusEx
	ms.Length = uint32(unsafe.Sizeof(ms))
	r1, _, _ := procGlobalMemoryStatusEx.Call(uintptr(unsafe.Pointer(&ms)))
	if r1 == 0 {
		return HostResources{LogicalCPUs: cpus, TotalRAMBytes: 0, MemoryLoadPercent: 101}
	}
	ml := ms.MemoryLoad
	if ml > 100 {
		ml = 100
	}
	return HostResources{LogicalCPUs: cpus, TotalRAMBytes: ms.TotalPhys, MemoryLoadPercent: ml}
}
