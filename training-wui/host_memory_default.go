//go:build !windows && !linux

package main

// hostMemoryForPreflight returns physical RAM facts when the OS exposes them; otherwise ok=false.
func hostMemoryForPreflight() (totalBytes uint64, availableBytes uint64, ok bool) {
	return 0, 0, false
}
