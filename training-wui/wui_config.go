package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	toml "github.com/pelletier/go-toml/v2"
)

const defaultWUIConfigRel = "configs/wui.toml"

type wuiRuntimeProfile struct {
	TernaryImpl         string `toml:"ternary_impl"`
	TritPackImpl        string `toml:"trit_pack_impl"`
	MemoryEncodeImpl    string `toml:"memory_encode_impl"`
	WasmExecImpl        string `toml:"wasm_exec_impl"`
	CascadeRLImpl       string `toml:"cascade_rl_impl"`
	TPEMNativeBundle    string `toml:"tpem_native_bundle"`
	NativeStrictEnabled bool   `toml:"native_strict_enabled"`
}

// wuiFileRoot is the on-disk shape of configs/wui.toml (nested tables).
type wuiFileRoot struct {
	WUI struct {
		TrainingRuntimeMode string `toml:"training_runtime_mode"`
		GRPCAddr            string `toml:"grpc_addr"`
		Runpod              struct {
			SSHUser    string `toml:"ssh_user"`
			RemoteDir  string `toml:"remote_dir"`
			SSHKeyPath string `toml:"ssh_key_path"`
		} `toml:"runpod"`
		RuntimeProfile wuiRuntimeProfile `toml:"runtime_profile"`
	} `toml:"wui"`
}

// WUIResolved holds merged file + CLI flags (operational config only).
type WUIResolved struct {
	ConfigPath          string
	TrainingRuntimeMode string
	GRPCAddr            string
	RunpodSSHUser       string
	RunpodRemoteDir     string
	RunpodSSHKeyPath    string
	RuntimeProfile      wuiRuntimeProfile
}

var wuiResolved WUIResolved

func defaultGRPCAddr() string {
	return "127.0.0.1:50061"
}

func effectiveTrainingRuntimeMode(raw string) string {
	m := strings.ToLower(strings.TrimSpace(raw))
	switch m {
	case "":
		// Native (C++ gRPC) is the default training path; use "auto" or "python" explicitly when needed.
		return "native"
	case "optimized":
		return "native"
	default:
		return m
	}
}

func loadWUIConfig(repoRoot, configPath string, flagMode, flagGRPC *string) error {
	path := strings.TrimSpace(configPath)
	if path == "" {
		path = filepath.Join(repoRoot, filepath.FromSlash(defaultWUIConfigRel))
	} else {
		if !filepath.IsAbs(path) {
			path = filepath.Join(repoRoot, filepath.FromSlash(path))
		}
	}

	var file wuiFileRoot
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) && strings.TrimSpace(configPath) == "" {
			// No default file: use hardcoded defaults
			wuiResolved = WUIResolved{
				ConfigPath:          "(defaults)",
				TrainingRuntimeMode: "native",
				GRPCAddr:            defaultGRPCAddr(),
				RunpodSSHUser:       "root",
				RunpodRemoteDir:     "/workspace/qminiwasm-core",
				RunpodSSHKeyPath:    "",
			}
			wuiResolved.RuntimeProfile = wuiRuntimeProfile{
				TernaryImpl: "auto", TritPackImpl: "auto", MemoryEncodeImpl: "auto",
				WasmExecImpl: "auto", CascadeRLImpl: "auto", TPEMNativeBundle: "0",
			}
		} else {
			return fmt.Errorf("wui config %q: %w", path, err)
		}
	} else {
		if err := toml.Unmarshal(data, &file); err != nil {
			return fmt.Errorf("parse wui config %q: %w", path, err)
		}
		wuiResolved = WUIResolved{
			ConfigPath:          path,
			TrainingRuntimeMode: strings.TrimSpace(file.WUI.TrainingRuntimeMode),
			GRPCAddr:            strings.TrimSpace(file.WUI.GRPCAddr),
			RunpodSSHUser:       strings.TrimSpace(file.WUI.Runpod.SSHUser),
			RunpodRemoteDir:     strings.TrimSpace(file.WUI.Runpod.RemoteDir),
			RunpodSSHKeyPath:    strings.TrimSpace(file.WUI.Runpod.SSHKeyPath),
			RuntimeProfile:      file.WUI.RuntimeProfile,
		}
	}
	if wuiResolved.TrainingRuntimeMode == "" {
		wuiResolved.TrainingRuntimeMode = "native"
	}
	if wuiResolved.GRPCAddr == "" {
		wuiResolved.GRPCAddr = defaultGRPCAddr()
	}
	if wuiResolved.RunpodSSHUser == "" {
		wuiResolved.RunpodSSHUser = "root"
	}
	if wuiResolved.RunpodRemoteDir == "" {
		wuiResolved.RunpodRemoteDir = "/workspace/qminiwasm-core"
	}
	// CLI overrides file
	if flagMode != nil && strings.TrimSpace(*flagMode) != "" {
		wuiResolved.TrainingRuntimeMode = strings.TrimSpace(*flagMode)
	}
	if flagGRPC != nil && strings.TrimSpace(*flagGRPC) != "" {
		wuiResolved.GRPCAddr = strings.TrimSpace(*flagGRPC)
	}
	// Normalize mode
	wuiResolved.TrainingRuntimeMode = effectiveTrainingRuntimeMode(wuiResolved.TrainingRuntimeMode)
	// Defaults for runtime profile strings
	rp := &wuiResolved.RuntimeProfile
	if strings.TrimSpace(rp.TernaryImpl) == "" {
		rp.TernaryImpl = "auto"
	}
	if strings.TrimSpace(rp.TritPackImpl) == "" {
		rp.TritPackImpl = "auto"
	}
	if strings.TrimSpace(rp.MemoryEncodeImpl) == "" {
		rp.MemoryEncodeImpl = "auto"
	}
	if strings.TrimSpace(rp.WasmExecImpl) == "" {
		rp.WasmExecImpl = "auto"
	}
	if strings.TrimSpace(rp.CascadeRLImpl) == "" {
		rp.CascadeRLImpl = "auto"
	}
	if strings.TrimSpace(rp.TPEMNativeBundle) == "" {
		rp.TPEMNativeBundle = "0"
	}
	return nil
}

func trainingGRPCAddressResolved() string {
	return strings.TrimSpace(wuiResolved.GRPCAddr)
}

func trainingRuntimeModeRaw() string {
	return wuiResolved.TrainingRuntimeMode
}

// wuiRuntimeEnvForPython sets subprocess-visible vars so `python -m qminiwasm.engine` matches WUI TOML
// (legacy env keys; prefer configs/runtime.toml on the Python side long-term).
func wuiRuntimeEnvForPython() []string {
	rp := wuiResolved.RuntimeProfile
	ns := "0"
	if rp.NativeStrictEnabled {
		ns = "1"
	}
	return []string{
		"QMINIWASM_TRAINING_RUNTIME_MODE=" + trainingRuntimeModeRaw(),
		"QMINIWASM_TRAINING_GRPC_ADDR=" + trainingGRPCAddressResolved(),
		"QMINIWASM_TERNARY_IMPL=" + rp.TernaryImpl,
		"QMINIWASM_TRIT_PACK_IMPL=" + rp.TritPackImpl,
		"QMINIWASM_MEMORY_ENCODE_IMPL=" + rp.MemoryEncodeImpl,
		"QMINIWASM_WASM_EXEC_IMPL=" + rp.WasmExecImpl,
		"QMINIWASM_CASCADE_RL_IMPL=" + rp.CascadeRLImpl,
		"QMINIWASM_TPEM_NATIVE_BUNDLE=" + rp.TPEMNativeBundle,
		"QMINIWASM_NATIVE_STRICT=" + ns,
	}
}

// trainingEnginePickLocal returns whether a local run would use native gRPC (same rules as useNativeTrainingEngineGRPC for local).
func trainingEnginePickLocal() (useGRPC bool, reason string) {
	mode := trainingRuntimeModeRaw()
	switch mode {
	case "cpp", "grpc", "native":
		return true, "mode forces C++ gRPC training engine (foundation)"
	case "python":
		return false, "mode forces Python engine"
	case "auto":
		if !trainingGRPCReachable(600 * time.Millisecond) {
			return false, "auto: gRPC endpoint not reachable at " + trainingGRPCAddressResolved() + " — using Python"
		}
		return true, "auto: gRPC reachable — using C++ foundation engine (not full PyTorch curriculum)"
	default:
		return false, "unknown mode — using Python"
	}
}
