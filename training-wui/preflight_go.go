package main

import (
	"os"
	"strings"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingconfig"
)

// buildGoPreflightDegraded returns a JSON-shaped map similar to the Python torch/SYCL probe
// when Python is unavailable or QMW_WUI_PREFLIGHT_GO_ONLY is set.
func buildGoPreflightDegraded(
	configAbs, preflightAccel, quantumBackend, quantumPolicy, ibmBackendName string,
) map[string]any {
	accelFromFile := ""
	qbFromFile := ""
	var dModel, ioDModel, numBlocks int64
	if strings.TrimSpace(configAbs) != "" {
		if doc, err := trainingconfig.ParseTrainingDocFile(configAbs); err == nil {
			accelFromFile = strings.TrimSpace(doc.Hardware.Accelerator)
			qbFromFile = strings.TrimSpace(doc.Hardware.QuantumBackend)
			if doc.Model.DModel != nil {
				dModel = *doc.Model.DModel
			}
			if doc.Model.IoDModel != nil {
				ioDModel = *doc.Model.IoDModel
			}
			if doc.Model.NumTernaryBlocks != nil {
				numBlocks = *doc.Model.NumTernaryBlocks
			}
		}
	}

	requested := strings.TrimSpace(preflightAccel)
	if requested == "" {
		requested = accelFromFile
	}
	if requested == "" {
		requested = strings.TrimSpace(os.Getenv("ACCELERATOR"))
	}
	if requested == "" {
		requested = "auto"
	}

	qp := normalizeQuantumPolicy(quantumPolicy)
	qbEffective := effectiveQuantumBackendForPolicy(quantumBackend, qp, ibmBackendName)
	qbOut := qbEffective
	if qbOut == "" {
		qbOut = qbFromFile
	}
	if qbOut == "" {
		qbOut = "penny_lane"
	}

	qPolicyOut := qp
	if qPolicyOut == "" {
		qPolicyOut = "prefer_hardware_fallback"
	}
	if envQP := strings.TrimSpace(os.Getenv("QUANTUM_EXECUTION_POLICY")); envQP != "" && quantumPolicy == "" {
		qPolicyOut = normalizeQuantumPolicy(envQP)
		if qPolicyOut == "" {
			qPolicyOut = "prefer_hardware_fallback"
		}
	}

	tokenPresent := strings.TrimSpace(os.Getenv("IBM_QUANTUM_API_TOKEN")) != "" ||
		strings.TrimSpace(os.Getenv("QISKIT_IBM_TOKEN")) != ""

	ibm := map[string]any{
		"runtime_available":     false,
		"token_present":         tokenPresent,
		"connected":             false,
		"requested_backend":     firstNonEmpty(strings.TrimSpace(ibmBackendName), qbOut, "auto"),
		"selected_backend":      "",
		"backend_operational":   nil,
		"backend_pending_jobs":  nil,
		"backend_status_msg":    "",
		"usage_seconds":         nil,
		"usage_limit_seconds":   nil,
		"job_time_left_seconds": nil,
		"connect_error":         "IBM/Qiskit probe skipped (Go preflight only; install Python stack for live IBM status)",
	}

	return map[string]any{
		"python_exe":                     "",
		"config":                         configAbs,
		"requested_accelerator":          requested,
		"accelerator_from_config_file":   accelFromFile,
		"preflight_accelerator_override": strings.TrimSpace(preflightAccel),
		"resolved_torch_device":          "unavailable (Python+Torch probe not run)",
		"backend_policy": map[string]any{
			"selected_device": "unknown",
			"reason_code":     "go_preflight_only",
		},
		"sycl_backend_active":            false,
		"sycl_backend_status":            map[string]any{"backend": "unknown", "fallback_reason": "n/a"},
		"dpctl_device_count":             nil,
		"dpctl_error":                    "dpctl not queried (Go preflight only)",
		"quantum_backend":                qbOut,
		"quantum_backend_from_config":    qbFromFile,
		"quantum_policy":                 qPolicyOut,
		"default_runtime_profile":        "native_wui",
		"model_d_model":                  dModel,
		"model_io_d_model":               ioDModel,
		"model_num_ternary_blocks":       numBlocks,
		"python_version":                 "",
		"xpu_support_class":              nil,
		"xpu_device_name":                nil,
		"intel_xpu_stack_advisory":       "",
		"xpu_memory_allocated_bytes":     nil,
		"xpu_max_memory_allocated_bytes": nil,
		"ibm":                            ibm,
	}
}

func firstNonEmpty(a ...string) string {
	for _, s := range a {
		if strings.TrimSpace(s) != "" {
			return s
		}
	}
	return ""
}
