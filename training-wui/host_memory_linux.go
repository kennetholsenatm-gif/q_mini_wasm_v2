//go:build linux

package main

import (
	"bufio"
	"os"
	"strconv"
	"strings"
)

func hostMemoryForPreflight() (totalBytes uint64, availableBytes uint64, ok bool) {
	f, err := os.Open("/proc/meminfo")
	if err != nil {
		return 0, 0, false
	}
	defer f.Close()

	var totalKB, availKB uint64
	var haveTotal, haveAvail bool
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := sc.Text()
		if strings.HasPrefix(line, "MemTotal:") {
			fields := strings.Fields(line)
			if len(fields) >= 2 {
				if v, err := strconv.ParseUint(fields[1], 10, 64); err == nil {
					totalKB = v
					haveTotal = true
				}
			}
		}
		if strings.HasPrefix(line, "MemAvailable:") {
			fields := strings.Fields(line)
			if len(fields) >= 2 {
				if v, err := strconv.ParseUint(fields[1], 10, 64); err == nil {
					availKB = v
					haveAvail = true
				}
			}
		}
	}
	if !haveTotal {
		return 0, 0, false
	}
	total := totalKB * 1024
	var avail uint64
	if haveAvail {
		avail = availKB * 1024
	} else {
		avail = 0
	}
	return total, avail, true
}
