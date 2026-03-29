package edgeartifacts

import (
	"encoding/json"
	"fmt"
)

var tierNumToName = map[int]string{
	1: "micro",
	2: "meso",
	3: "macro",
	4: "workgroup",
	5: "enterprise_core",
}

type tierPreset struct {
	memory64Required     bool
	defaultMemory64MaxMB *float64
	tierNumber           string
}

var tierPresets = map[string]tierPreset{
	"micro": {
		tierNumber: "1", memory64Required: false, defaultMemory64MaxMB: nil,
	},
	"meso": {
		tierNumber: "2", memory64Required: false, defaultMemory64MaxMB: nil,
	},
	"macro": {
		tierNumber: "3", memory64Required: true, defaultMemory64MaxMB: float64Ptr(8192.0),
	},
	"workgroup": {
		tierNumber: "4", memory64Required: true, defaultMemory64MaxMB: float64Ptr(16384.0),
	},
	"enterprise_core": {
		tierNumber: "5", memory64Required: true, defaultMemory64MaxMB: float64Ptr(262144.0),
	},
}

func float64Ptr(f float64) *float64 { return &f }

// EdgeSchemaObject matches scripts/build_tpem_wasm_artifacts._edge_schema JSON (sort_keys for stable diff).
func EdgeSchemaObject(tier int) (map[string]any, error) {
	if tier < 1 || tier > 5 {
		return nil, fmt.Errorf("invalid tier %d (want 1–5)", tier)
	}
	name := tierNumToName[tier]
	p := tierPresets[name]
	useM64 := p.memory64Required || tier >= 3
	var m64 any
	if useM64 && p.defaultMemory64MaxMB != nil {
		m64 = *p.defaultMemory64MaxMB
	} else {
		m64 = nil
	}
	return map[string]any{
		"enclave_tier":         name,
		"enclave_tier_number":  p.tierNumber,
		"use_memory64":         useM64,
		"wasm_memory64_max_mb": m64,
		"kernel_wasm_features": []string{"wasm32"},
		"notes": "Trit kernel module is wasm32 (i32 linear memory). use_memory64 / " +
			"wasm_memory64_max_mb describe host runtime policy; a dedicated " +
			"memory64 kernel module is not shipped in this build.",
	}, nil
}

// EdgeSchemaJSON returns indented JSON with trailing newline (Python-compatible).
func EdgeSchemaJSON(tier int) ([]byte, error) {
	obj, err := EdgeSchemaObject(tier)
	if err != nil {
		return nil, err
	}
	b, err := json.MarshalIndent(obj, "", "  ")
	if err != nil {
		return nil, err
	}
	return append(b, '\n'), nil
}
