package main

import "testing"

func testConfigWithMaps(data map[string]map[string]interface{}, present map[string]map[string]struct{}) *Config {
	return &Config{
		data:    data,
		present: present,
	}
}

func TestResolveEffectiveTrainingParams_DerivesFromParents(t *testing.T) {
	cfg := testConfigWithMaps(
		map[string]map[string]interface{}{
			"training": {
				"batch_size":        512,
				"micro_batch_cap":   4096,
				"collect_floor":     256,
				"parallel_batches":  2,
				"collect_window_ms": 75,
			},
			"model": {
				"moe_experts":                   8192,
				"moe_top_k":                     256,
				"expert_internal_layers":        3,
				"moe_ff_active_internal_layers": 0,
			},
			"features": {
				"lazy_init": true,
			},
		},
		map[string]map[string]struct{}{
			"training": {
				"batch_size":        {},
				"micro_batch_cap":   {},
				"collect_floor":     {},
				"parallel_batches":  {},
				"collect_window_ms": {},
			},
			"model": {
				"moe_experts":            {},
				"moe_top_k":              {},
				"expert_internal_layers": {},
			},
			"features": {
				"lazy_init": {},
			},
		},
	)

	p := resolveEffectiveTrainingParams(cfg)
	if p.EffectiveTopK != 256 {
		t.Fatalf("effective top_k mismatch: got %d want 256", p.EffectiveTopK)
	}
	if p.EffectiveCollectLimit != 512 {
		t.Fatalf("effective collect_limit mismatch: got %d want 512", p.EffectiveCollectLimit)
	}
	if p.EffectiveTargetRoutesPerBatch != 131072 {
		t.Fatalf("effective target_routes_per_batch mismatch: got %d want 131072", p.EffectiveTargetRoutesPerBatch)
	}
	if p.EffectiveCollectMinRowsPerBatch != 512 {
		t.Fatalf("effective collect_min_rows_per_batch mismatch: got %d want 512", p.EffectiveCollectMinRowsPerBatch)
	}
	if p.EffectiveLazyInitInitialExperts != 256 {
		t.Fatalf("effective lazy_init_initial_experts mismatch: got %d want 256", p.EffectiveLazyInitInitialExperts)
	}
	if p.EffectiveMoeFFActiveInternalLayers != 3 {
		t.Fatalf("effective moe_ff_active_internal_layers mismatch: got %d want 3", p.EffectiveMoeFFActiveInternalLayers)
	}
}

func TestResolveEffectiveTrainingParams_RespectsExplicitOverrides(t *testing.T) {
	cfg := testConfigWithMaps(
		map[string]map[string]interface{}{
			"training": {
				"batch_size":                 512,
				"micro_batch_cap":            4096,
				"collect_floor":              256,
				"target_routes_per_batch":    90000,
				"collect_min_rows_per_batch": 128,
				"lazy_init_initial_experts":  1024,
			},
			"model": {
				"moe_experts":                   8192,
				"moe_top_k":                     256,
				"expert_internal_layers":        3,
				"moe_ff_active_internal_layers": 2,
			},
			"features": {
				"lazy_init": true,
			},
		},
		map[string]map[string]struct{}{
			"training": {
				"batch_size":                 {},
				"micro_batch_cap":            {},
				"collect_floor":              {},
				"target_routes_per_batch":    {},
				"collect_min_rows_per_batch": {},
				"lazy_init_initial_experts":  {},
			},
			"model": {
				"moe_experts":                   {},
				"moe_top_k":                     {},
				"expert_internal_layers":        {},
				"moe_ff_active_internal_layers": {},
			},
			"features": {
				"lazy_init": {},
			},
		},
	)

	p := resolveEffectiveTrainingParams(cfg)
	if p.EffectiveTargetRoutesPerBatch != 90000 {
		t.Fatalf("explicit target_routes_per_batch ignored: got %d want 90000", p.EffectiveTargetRoutesPerBatch)
	}
	if p.EffectiveCollectMinRowsPerBatch != 128 {
		t.Fatalf("explicit collect_min_rows_per_batch ignored: got %d want 128", p.EffectiveCollectMinRowsPerBatch)
	}
	if p.EffectiveLazyInitInitialExperts != 1024 {
		t.Fatalf("explicit lazy_init_initial_experts ignored: got %d want 1024", p.EffectiveLazyInitInitialExperts)
	}
	if p.EffectiveMoeFFActiveInternalLayers != 2 {
		t.Fatalf("explicit moe_ff_active_internal_layers ignored: got %d want 2", p.EffectiveMoeFFActiveInternalLayers)
	}
}
