package main

import (
	"testing"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingrpc"
)

func TestComputeNativeColdStartMemoryEstimateMap_small(t *testing.T) {
	cfg := &trainingrpc.TrainingConfig{
		DModel:            512,
		IoDModel:          512,
		NumTernaryBlocks:  1,
		BatchSize:         8,
	}
	m := computeNativeColdStartMemoryEstimateMap(cfg)
	if m == nil {
		t.Fatal("nil map")
	}
	p := m["parameter_bytes_fp32"].(uint64)
	if p == 0 {
		t.Fatalf("expected param bytes > 0, got %d", p)
	}
	// 1 block: d*d + 2*d floats, io==d no stem
	// 512*512 + 1024 = 262144 + 1024 = 263168 floats * 4
	wantFloats := uint64(512*512 + 2*512)
	if p != wantFloats*4 {
		t.Fatalf("parameter_bytes_fp32: got %d want %d", p, wantFloats*4)
	}
}

func TestComputeNativeColdStartMemoryEstimateMap_stem(t *testing.T) {
	cfg := &trainingrpc.TrainingConfig{
		DModel:            128,
		IoDModel:          64,
		NumTernaryBlocks:  1,
		BatchSize:         4,
	}
	m := computeNativeColdStartMemoryEstimateMap(cfg)
	p := m["parameter_bytes_fp32"].(uint64)
	// stem/head: 2*64*128 + 128 + 64 = 16384+192 = 16576
	// block: 128*128 + 256 = 16640
	// total floats 16576 + 16640 = 33216
	want := (16576 + 16640) * 4
	if p != uint64(want) {
		t.Fatalf("got %d want %d", p, want)
	}
}
