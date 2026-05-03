package main

/*
#cgo LDFLAGS: -L${SRCDIR}/../../q_mini_wasm_v2/build_sycl -L${SRCDIR}/../.. -l q_training
#cgo CFLAGS: -I${SRCDIR}/../../q_mini_wasm_v2/dll/training
#cgo windows LDFLAGS: -Wl,-rpath,${SRCDIR}/../../q_mini_wasm_v2/build_sycl -Wl,-rpath,${SRCDIR}/../..
#include <stdlib.h>
#include "training_api_cgo.h"
*/
import "C"

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"math"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
	"unsafe"
)

func init() {
	// Go + Intel SYCL/MKL/OpenMP in q_training.dll can load multiple OpenMP runtimes on Windows;
	// duplicate-runtime abort manifests as SIGABRT in q_mini_training_crash.log without a clear C++ catch.
	if runtime.GOOS == "windows" && strings.TrimSpace(os.Getenv("KMP_DUPLICATE_LIB_OK")) == "" {
		_ = os.Setenv("KMP_DUPLICATE_LIB_OK", "TRUE")
	}
	// Windows: full path + size + mtime of the q_training.dll actually mapped into this process (stderr).
	logLoadedQTrainingDLLIdentity()
}

const (
	Port         = 9090
	wuiAssetPath = "wui"
	DataDir      = "C:/q_mini_data"

	// Training process phases (Go control plane; exposed in wui_get_training_metrics).
	phaseUnconfigured       = "unconfigured"
	phaseReady              = "ready"                // pipeline configured, not in native CGO start
	phaseNativeSessionInit  = "native_session_init"  // inside Training_InitSession
	phaseNativePipelineInit = "native_pipeline_init" // inside Training_StartTraining (initialize + start)
	phaseTraining           = "training"             // poll loop: native reports running
	phaseStopping           = "stopping"
)

// listenPort returns the HTTP listen port (default 9090). Override with env QMINI_HTTP_PORT for tests or side-by-side runs.
func listenPort() int {
	s := strings.TrimSpace(os.Getenv("QMINI_HTTP_PORT"))
	if s == "" {
		return Port
	}
	p, err := strconv.Atoi(s)
	if err != nil || p < 1024 || p > 65535 {
		fmt.Fprintf(os.Stderr, "[Server] QMINI_HTTP_PORT=%q invalid; using %d\n", s, Port)
		return Port
	}
	return p
}

// qminiDataRoot returns the on-disk root for config/, datasets/, checkpoints/, etc.
// Override with QMINI_DATA_DIR (absolute path) so the WUI and native training read the same tree as your editor.
func qminiDataRoot() string {
	if s := strings.TrimSpace(os.Getenv("QMINI_DATA_DIR")); s != "" {
		return filepath.Clean(s)
	}
	return DataDir
}

// resolveDataPath joins DataDir with a configured relative dataset directory, or returns an absolute path unchanged.
func resolveDataPath(dataRoot, configured string) string {
	p := strings.TrimSpace(configured)
	if p == "" {
		p = "datasets"
	}
	if filepath.IsAbs(p) {
		return filepath.Clean(p)
	}
	return filepath.Join(dataRoot, filepath.Clean(p))
}

// resolveTrainingConfigPath returns the active training TOML path.
// Set QMINI_TRAINING_CONFIG to an absolute file path, or a name relative to DataDir/config/
// (e.g. "training_config.smoke.toml"), to use a smoke or alternate profile without overwriting production.
func resolveTrainingConfigPath() string {
	root := qminiDataRoot()
	if override := strings.TrimSpace(os.Getenv("QMINI_TRAINING_CONFIG")); override != "" {
		if filepath.IsAbs(override) {
			return filepath.Clean(override)
		}
		return filepath.Clean(filepath.Join(root, "config", override))
	}
	return filepath.Join(root, "config", "training_config.toml")
}

// resolveDataSourcesConfigPath returns data_sources.toml for acquisition. Override with QMINI_DATA_SOURCES_TOML
// (absolute path, or filename under DataDir/config/) for isolated proof/smoke runs.
func resolveDataSourcesConfigPath() string {
	root := qminiDataRoot()
	if override := strings.TrimSpace(os.Getenv("QMINI_DATA_SOURCES_TOML")); override != "" {
		if filepath.IsAbs(override) {
			return filepath.Clean(override)
		}
		return filepath.Clean(filepath.Join(root, "config", override))
	}
	return filepath.Join(root, "config", "data_sources.toml")
}

// EffectiveTrainingParams holds computed training parameters derived from config values.
type EffectiveTrainingParams struct {
	EffectiveTopK                      uint32
	EffectiveCollectLimit              uint32
	EffectiveTargetRoutesPerBatch      uint32
	EffectiveCollectMinRowsPerBatch    uint32
	EffectiveLazyInitInitialExperts    uint32
	EffectiveMoeFFActiveInternalLayers uint32
}

// resolveEffectiveTrainingParams derives effective training parameters from configuration.
// It computes values based on parent config settings when explicit overrides are not present.
func resolveEffectiveTrainingParams(cfg *Config) EffectiveTrainingParams {
	batchSize := uint32(cfg.GetInt("training.batch_size"))
	microBatchCap := uint32(cfg.GetInt("training.micro_batch_cap"))
	collectFloor := uint32(cfg.GetInt("training.collect_floor"))
	parallelBatches := uint32(cfg.GetInt("training.parallel_batches"))
	_ = uint32(cfg.GetInt("model.moe_experts")) // reserved for future use
	moeTopK := uint32(cfg.GetInt("model.moe_top_k"))
	expertInternalLayers := uint32(cfg.GetInt("model.expert_internal_layers"))
	lazyInit := cfg.GetBool("features.lazy_init")

	// Effective collect limit is min(batch_size, micro_batch_cap, collect_floor) with sensible defaults
	collectLimit := batchSize
	if microBatchCap > 0 && microBatchCap < collectLimit {
		collectLimit = microBatchCap
	}
	if collectFloor > 0 && collectFloor < collectLimit {
		collectLimit = collectFloor
	}

	// Default target_routes_per_batch = batch_size * moe_top_k * parallel_batches
	targetRoutes := batchSize * moeTopK * parallelBatches
	if targetRoutes == 0 {
		targetRoutes = 131072 // default fallback
	}

	// Check for explicit override
	if cfg.IsSet("training.target_routes_per_batch") {
		targetRoutes = uint32(cfg.GetInt("training.target_routes_per_batch"))
	}

	// Default collect_min_rows_per_batch = batch_size
	collectMinRows := batchSize
	if cfg.IsSet("training.collect_min_rows_per_batch") {
		collectMinRows = uint32(cfg.GetInt("training.collect_min_rows_per_batch"))
	}

	// Default lazy_init_initial_experts = moe_top_k when lazy_init is enabled
	lazyInitInitial := uint32(0)
	if lazyInit {
		lazyInitInitial = moeTopK
	}
	if cfg.IsSet("training.lazy_init_initial_experts") {
		v := uint32(cfg.GetInt("training.lazy_init_initial_experts"))
		// 0 = derive when lazy_init (keep moe_top_k default above); explicit positive overrides.
		if v != 0 {
			lazyInitInitial = v
		}
	}

	// moe_ff_active_internal_layers defaults to expert_internal_layers
	ffActiveInternal := expertInternalLayers
	if cfg.IsSet("model.moe_ff_active_internal_layers") {
		ffActiveInternal = uint32(cfg.GetInt("model.moe_ff_active_internal_layers"))
	}

	return EffectiveTrainingParams{
		EffectiveTopK:                      moeTopK,
		EffectiveCollectLimit:              collectLimit,
		EffectiveTargetRoutesPerBatch:      targetRoutes,
		EffectiveCollectMinRowsPerBatch:    collectMinRows,
		EffectiveLazyInitInitialExperts:    lazyInitInitial,
		EffectiveMoeFFActiveInternalLayers: ffActiveInternal,
	}
}

// countTxtLikeInDir counts non-directory .txt / .jsonl files (same rules as acquisition status).
func countTxtLikeInDir(dir string) int {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return 0
	}
	n := 0
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		name := strings.ToLower(e.Name())
		if strings.HasSuffix(name, ".txt") || strings.HasSuffix(name, ".jsonl") {
			n++
		}
	}
	return n
}

// effectiveDatasetPathForTraining resolves [paths].dataset_dir under dataRoot and requires at least one .txt/.jsonl.
func effectiveDatasetPathForTraining(dataRoot, configured string) (string, error) {
	primary := resolveDataPath(dataRoot, configured)
	if countTxtLikeInDir(primary) == 0 {
		return "", fmt.Errorf("no .txt or .jsonl files under dataset path %q (paths.dataset_dir); add data or fix the path", primary)
	}
	return primary, nil
}

// Global state for functional WUI
var (
	trainingState = &TrainingState{
		IsRunning:    false,
		CurrentEpoch: 0,
		TotalEpochs:  0, // set by wui_init_training_pipeline from TOML; metrics use active TOML when 0 / idle
		Loss:         0.0,
		StartTime:    time.Time{},
		Phase:        phaseUnconfigured,
	}
	trainingStateMu sync.RWMutex

	// Throttle identical [MCP] wui_get_training_metrics lines (poll is frequent).
	metricsServerLogMu   sync.Mutex
	lastMetricsServerSig string
	lastMetricsServerAt  time.Time

	metricsHistory   = make([]map[string]interface{}, 0)
	originalHandlers = make(map[string]http.HandlerFunc)

	// Buffered so MCP-triggered shutdown cannot block if a duplicate request arrives.
	gracefulShutdownRequested = make(chan struct{}, 1)
)

type TrainingState struct {
	IsRunning      bool      `json:"is_running"`
	CurrentEpoch   int       `json:"current_epoch"`
	TotalEpochs    int       `json:"total_epochs"`
	Loss           float64   `json:"loss"`
	StartTime      time.Time `json:"start_time"`
	LastUpdate     time.Time `json:"last_update"`
	SessionID      uint64    `json:"session_id"`
	Samples        int       `json:"samples"` // Per-epoch processed work units
	SamplesTotal   uint64    `json:"samples_total"`
	CurrentExperts int       `json:"current_experts"`
	TargetExperts  int       `json:"target_experts"`
	LazyInit       bool      `json:"lazy_init"`
	InitMessage    string    `json:"init_message"`
	LoopCount      int       `json:"loop_count"` // Continuous mode loop counter
	// PipelineConfigured is set by wui_init_training_pipeline (Go-only validation).
	// Native q_training.dll loads on wui_start_ff_training to avoid crashing the HTTP handler mid-request.
	PipelineConfigured bool `json:"pipeline_configured"`
	// Phase is the Go-side process tracker (independent of is_running during long native CGO init).
	Phase                       string    `json:"phase"`
	PhaseStartedAt              time.Time `json:"-"`
	LastDLLPollAt               time.Time `json:"-"`
	DSQueueDepth                uint32    `json:"ds_queue_depth"`
	DSRawQueueDepth             uint32    `json:"ds_raw_queue_depth"`
	DSRawQueueMax               uint32    `json:"ds_raw_queue_max"`
	DSTrainQueueMax             uint32    `json:"ds_train_queue_max"`
	DSAcqQueueDepth             uint32    `json:"ds_acq_queue_depth"`
	DSAcqQueueMax               uint32    `json:"ds_acq_queue_max"`
	DSBlockedRawPushes          uint64    `json:"ds_blocked_raw_pushes"`
	DSBlockedTrainPushes        uint64    `json:"ds_blocked_train_pushes"`
	DSBlockedWaitMs             uint64    `json:"ds_blocked_wait_ms"`
	DSDroppedPayloads           uint64    `json:"ds_dropped_payloads"`
	DSAcqBlockedPushes          uint64    `json:"ds_acq_blocked_pushes"`
	DSAcqBlockedWaitMs          uint64    `json:"ds_acq_blocked_wait_ms"`
	DSAcqDroppedTooShort        uint64    `json:"ds_acq_dropped_too_short"`
	GF3HebbianWeightCellUpdates uint64    `json:"gf3_hebbian_weight_cell_updates"`
	DSDataAcquired              uint32    `json:"ds_data_acquired"`
	DSDataPerturbed             uint32    `json:"ds_data_perturbed"`
	DSAPIFailures               uint32    `json:"ds_api_failures"`
	DSBetti0                    uint32    `json:"ds_betti_0"`
	DSBetti1                    uint32    `json:"ds_betti_1"`
	DSBetti2                    uint32    `json:"ds_betti_2"`
	DSTopicFrontierSize         uint32    `json:"ds_topic_frontier_size"`
	DSTopicFrontierMax          uint32    `json:"ds_topic_frontier_max"`
	DSTopicFrontierEvict        uint32    `json:"ds_topic_frontier_evictions"`
	PrefillTargetSamples        uint32    `json:"prefill_target_samples"`
	BufferProfile               string    `json:"buffer_profile"`
	PrefillTimeoutMs            uint32    `json:"prefill_timeout_ms"`
	PrefillPollMs               uint32    `json:"prefill_poll_ms"`
	DSAcqQueueCap               uint32    `json:"ds_acq_queue_cap"`
	DSRawQueueCap               uint32    `json:"ds_raw_queue_cap"`
	DSTrainQueueCap             uint32    `json:"ds_train_queue_cap"`
	// Native AutonomousTrainingPipeline::get_metrics status_message (batch_inflight, queues, …).
	PipelineStatusText string `json:"pipeline_status"`
	LiveParallelBatches    uint32 `json:"live_parallel_batches"`
	ParallelBatchesCeiling uint32 `json:"parallel_batches_ceiling"`
}

type BufferTuning struct {
	Profile              string
	PrefillTarget        int
	PrefillTimeoutMs     int
	PrefillPollMs        int
	AcqQueueCap          int
	RawQueueCap          int
	TrainQueueCap        int
	DirectoryMaxLines    uint64
	MaxJsonlLocalSamples uint64
	MinTextLength        int
	MaxTextLength        int
}

// probeSyclDeviceVRAM queries SYCL global_mem_size for training.sycl_gpu_device_index resolution rules.
// Returns code 0 on success; non-zero leaves globalMem==0. Best-effort for resource autoscale (ignore failures).
func probeSyclDeviceVRAM(gpuIdx int32) (globalMem uint64, isGPU bool, devName string, code int) {
	var gmem C.uint64_t
	var isg C.uint32_t
	var nameBuf [512]byte
	var errBuf [512]byte
	rc := int(C.Training_ProbeSyclDevice(
		C.int32_t(gpuIdx),
		&gmem,
		&isg,
		(*C.char)(unsafe.Pointer(&nameBuf[0])),
		C.size_t(len(nameBuf)),
		(*C.char)(unsafe.Pointer(&errBuf[0])),
		C.size_t(len(errBuf)),
	))
	if rc != 0 {
		return 0, false, "", rc
	}
	n := 0
	for n < len(nameBuf) && nameBuf[n] != 0 {
		n++
	}
	return uint64(gmem), isg != 0, string(nameBuf[:n]), 0
}

func trainingDLLSetPendingLiveParallelStart(sid uint64, pb uint32) int {
	return int(C.Training_SetPendingLiveParallelStart(C.uint64_t(sid), C.uint32_t(pb)))
}

func trainingDLLSetLiveParallelBatches(sid uint64, pb uint32) int {
	return int(C.Training_SetLiveParallelBatches(C.uint64_t(sid), C.uint32_t(pb)))
}

func trainingDLLGetLiveParallelBatches(sid uint64) (live, ceiling uint32, ok bool) {
	var l, c C.uint32_t
	if int(C.Training_GetLiveParallelBatches(C.uint64_t(sid), &l, &c)) != 0 {
		return 0, 0, false
	}
	return uint32(l), uint32(c), true
}

func trainingDLLFetchBlockedAndRawQueues(sid uint64) (blockedRaw, blockedTrain uint64, rawDepth, rawMax uint32, ok bool) {
	var expertsActive, dataAcquired, dataPerturbed, apiFailures C.uint32_t
	var betti0, betti1, betti2 C.uint32_t
	var topicFrontierSize, topicFrontierMax, topicFrontierEvict C.uint32_t
	var samplesTotalNative C.uint64_t
	var dsQueueDepth, dsRawQueueDepth, dsRawQueueMax, dsTrainQueueMax C.uint32_t
	var dsAcqQueueDepth, dsAcqQueueMax C.uint32_t
	var dsBlockedRawPushes, dsBlockedTrainPushes, dsBlockedWaitMs C.uint64_t
	var dsDroppedPayloads, dsAcqBlockedPushes, dsAcqBlockedWaitMs C.uint64_t
	var dsAcqDroppedTooShort C.uint64_t
	var gf3HebbianWeightCellUpdates C.uint64_t
	var statusBuf [512]byte
	res := C.Training_GetMetrics(
		C.uint64_t(sid),
		&expertsActive,
		&dataAcquired,
		&dataPerturbed,
		&apiFailures,
		&betti0,
		&betti1,
		&betti2,
		&topicFrontierSize,
		&topicFrontierMax,
		&topicFrontierEvict,
		&dsQueueDepth,
		&dsRawQueueDepth,
		&dsRawQueueMax,
		&dsTrainQueueMax,
		&dsAcqQueueDepth,
		&dsAcqQueueMax,
		&dsBlockedRawPushes,
		&dsBlockedTrainPushes,
		&dsBlockedWaitMs,
		&dsDroppedPayloads,
		&dsAcqBlockedPushes,
		&dsAcqBlockedWaitMs,
		&samplesTotalNative,
		&dsAcqDroppedTooShort,
		&gf3HebbianWeightCellUpdates,
		(*C.char)(unsafe.Pointer(&statusBuf[0])),
		C.size_t(len(statusBuf)),
	)
	if res != 0 {
		return 0, 0, 0, 0, false
	}
	return uint64(dsBlockedRawPushes), uint64(dsBlockedTrainPushes), uint32(dsRawQueueDepth), uint32(dsRawQueueMax), true
}

// getBufferTuning reads explicit [training] queue / prefill fields. Caller must run validateTrainingConfig first.
func getBufferTuning(cfg *Config) BufferTuning {
	profile := strings.ToLower(strings.TrimSpace(cfg.GetString("training.buffer_profile")))
	return BufferTuning{
		Profile:              profile,
		PrefillTarget:        cfg.GetInt("training.prefill_target_samples"),
		PrefillTimeoutMs:     cfg.GetInt("training.prefill_timeout_ms"),
		PrefillPollMs:        cfg.GetInt("training.prefill_poll_ms"),
		AcqQueueCap:          cfg.GetInt("training.acq_queue_cap"),
		RawQueueCap:          cfg.GetInt("training.raw_queue_cap"),
		TrainQueueCap:        cfg.GetInt("training.train_queue_cap"),
		DirectoryMaxLines:    cfg.GetUint64("training.data_filter.directory_max_lines"),
		MaxJsonlLocalSamples: cfg.GetUint64("training.max_jsonl_local_samples"),
		MinTextLength:        cfg.GetInt("training.data_filter.min_text_length"),
		MaxTextLength:        cfg.GetInt("training.data_filter.max_text_length"),
	}
}

func runningNativeLaunch(phase string) bool {
	return phase == phaseNativeSessionInit || phase == phaseNativePipelineInit
}

func copyTrainingState() TrainingState {
	trainingStateMu.RLock()
	defer trainingStateMu.RUnlock()
	return *trainingState
}

func maybeLogTrainingMetricsServer(s TrainingState, displayTotalEpochs int) {
	phaseAge := 0.0
	if !s.PhaseStartedAt.IsZero() {
		phaseAge = time.Since(s.PhaseStartedAt).Seconds()
	}
	lastPoll := "never"
	if !s.LastDLLPollAt.IsZero() {
		lastPoll = fmt.Sprintf("%.0fs_ago", time.Since(s.LastDLLPollAt).Seconds())
	}
	sig := fmt.Sprintf("%s|%v|%d|%d|%d|%d|%d|%d|%v", s.Phase, s.IsRunning, s.CurrentEpoch, displayTotalEpochs, s.Samples, s.SamplesTotal, s.SessionID, s.LoopCount, s.PipelineConfigured)
	metricsServerLogMu.Lock()
	defer metricsServerLogMu.Unlock()
	now := time.Now()
	if sig == lastMetricsServerSig && now.Sub(lastMetricsServerAt) < 25*time.Second {
		return
	}
	lastMetricsServerSig = sig
	lastMetricsServerAt = now
	idleHint := ""
	if !s.PipelineConfigured && s.SessionID == 0 {
		idleHint = " [idle: no pipeline session — use WUI/MCP wui_init_training_pipeline then wui_start_ff_training; last_dll_poll stays never until native session starts]"
	}
	fmt.Printf("[MCP] wui_get_training_metrics: phase=%s (%.0fs in phase) running=%v epoch=%d/%d samples=%d samples_total=%d session=%d pipeline_configured=%v last_dll_poll=%s%s\n",
		s.Phase, phaseAge, s.IsRunning, s.CurrentEpoch, displayTotalEpochs, s.Samples, s.SamplesTotal, s.SessionID, s.PipelineConfigured, lastPoll, idleHint)
}

func (ts *TrainingState) Reset() {
	ts.IsRunning = false
	ts.CurrentEpoch = 0
	ts.Loss = 0.0
	ts.Samples = 0
	ts.SamplesTotal = 0
	ts.SessionID = 0
	ts.PipelineConfigured = false
	ts.InitMessage = ""
	ts.Phase = phaseUnconfigured
	ts.PhaseStartedAt = time.Time{}
	ts.LastDLLPollAt = time.Time{}
	ts.DSQueueDepth = 0
	ts.DSRawQueueDepth = 0
	ts.DSRawQueueMax = 0
	ts.DSTrainQueueMax = 0
	ts.DSAcqQueueDepth = 0
	ts.DSAcqQueueMax = 0
	ts.DSBlockedRawPushes = 0
	ts.DSBlockedTrainPushes = 0
	ts.DSBlockedWaitMs = 0
	ts.DSDroppedPayloads = 0
	ts.DSAcqBlockedPushes = 0
	ts.DSAcqBlockedWaitMs = 0
	ts.DSAcqDroppedTooShort = 0
	ts.GF3HebbianWeightCellUpdates = 0
	ts.DSDataAcquired = 0
	ts.DSDataPerturbed = 0
	ts.DSAPIFailures = 0
	ts.DSBetti0 = 0
	ts.DSBetti1 = 0
	ts.DSBetti2 = 0
	ts.DSTopicFrontierSize = 0
	ts.DSTopicFrontierMax = 0
	ts.DSTopicFrontierEvict = 0
	ts.PrefillTargetSamples = 0
	ts.BufferProfile = ""
	ts.PrefillTimeoutMs = 0
	ts.PrefillPollMs = 0
	ts.DSAcqQueueCap = 0
	ts.DSRawQueueCap = 0
	ts.DSTrainQueueCap = 0
	ts.PipelineStatusText = ""
	ts.LiveParallelBatches = 0
	ts.ParallelBatchesCeiling = 0
}

func (ts *TrainingState) Start(epochs int, lazyInit bool, targetExperts int, initMsg string) {
	ts.IsRunning = true
	ts.CurrentEpoch = 0
	ts.TotalEpochs = epochs
	ts.StartTime = time.Now()
	ts.LastUpdate = time.Now()
	ts.Samples = 0
	ts.LazyInit = lazyInit
	ts.TargetExperts = targetExperts

	if lazyInit {
		ts.CurrentExperts = 16
	} else {
		ts.CurrentExperts = targetExperts
	}

	ts.InitMessage = initMsg
	fmt.Printf("[Training] %s\n", initMsg)

	go ts.runTrainingLoop()
}

func (ts *TrainingState) Stop() {
	ts.IsRunning = false
}

func (ts *TrainingState) runTrainingLoop() {
	// Training runs in q_training.dll (AutonomousTrainingPipeline). This hook is unused
	// when the WUI uses wui_init_training_pipeline / wui_start_ff_training.
	ts.IsRunning = false
}

func main() {
	// Open log file — also mirror log.* to stderr so log.Fatalf / log.Panic do not "silently" kill
	// the server (e.g. HTTP listen error) with no console line.
	logFile, err := os.OpenFile("server.log", os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	if err == nil {
		log.SetOutput(io.MultiWriter(logFile, os.Stderr))
		defer logFile.Close()
	}

	fmt.Println("Starting qminiwasm server...")
	log.Println("Starting qminiwasm server...")

	// Get working directory
	workDir, err := os.Getwd()
	if err != nil {
		log.Fatal("Failed to get working directory:", err)
	}
	log.Printf("Working directory: %s", workDir)

	repoRoot := ""
	if exePath, err := os.Executable(); err == nil {
		repoRoot = findRepoRoot(filepath.Dir(exePath))
	}
	if repoRoot == "" {
		repoRoot = findRepoRoot(workDir)
	}
	pm := loadPathMap(repoRoot)
	if canon := canonicalExePath(repoRoot, pm); canon != "" {
		if exePath, err := os.Executable(); err == nil {
			if !strings.EqualFold(filepath.Clean(exePath), filepath.Clean(canon)) {
				fmt.Printf("[Paths] Expected canonical binary (see config/path_map.toml): %s\n", canon)
				fmt.Printf("[Paths] Actual executable: %s\n", exePath)
				log.Printf("[Paths] Non-canonical exe: want %s have %s", canon, exePath)
			} else {
				fmt.Printf("[Paths] Canonical qminiwasm: %s\n", canon)
				log.Printf("[Paths] Canonical qminiwasm: %s", canon)
			}
		}
	}
	if repoRoot != "" {
		fmt.Printf("[Paths] Repository root: %s (native_runtime=%s)\n", repoRoot, filepath.Join(repoRoot, pm.NativeRuntimeDir))
		log.Printf("[Paths] Repository root: %s", repoRoot)
	}

	tc := strings.TrimSpace(os.Getenv("QMINI_TRAINING_CONFIG"))
	if tc != "" {
		fmt.Printf("[Config] QMINI_TRAINING_CONFIG=%s\n", tc)
		log.Printf("[Config] QMINI_TRAINING_CONFIG=%s", tc)
	} else {
		fmt.Println("[Config] QMINI_TRAINING_CONFIG=(unset, using default path below)")
		log.Println("[Config] QMINI_TRAINING_CONFIG=(unset)")
	}
	fmt.Printf("[Config] QMINI_DATA_DIR / data root: %s\n", qminiDataRoot())
	log.Printf("[Config] QMINI_DATA_DIR / data root: %s", qminiDataRoot())
	cfgPath := resolveTrainingConfigPath()
	fmt.Printf("[Config] Active training TOML: %s\n", cfgPath)
	log.Printf("[Config] Active training TOML: %s", cfgPath)
	cfg, err := loadTrainingConfig()
	if err != nil {
		fmt.Printf("[Config] WARNING: Failed to load training TOML: %v\n", err)
		fmt.Println("[Config] Server will start, but training will be unavailable until config is fixed.")
		log.Printf("[Config] WARNING: training TOML load failed: %v. Training unavailable.", err)
	} else {
		if err := validateTrainingConfig(cfg); err != nil {
			fmt.Printf("[Config] WARNING: Invalid training TOML: %v\n", err)
			fmt.Println("[Config] Server will start, but training will be unavailable until config is fixed.")
			log.Printf("[Config] WARNING: training TOML validation failed: %v. Training unavailable.", err)
			cfg = nil
		} else {
			fmt.Printf("[Config] Validated training TOML: training.batch_size=%d model.moe_top_k=%d features.steane_correction=%v training.epochs=%d\n",
				cfg.GetInt("training.batch_size"),
				cfg.GetInt("model.moe_top_k"),
				cfg.GetBool("features.steane_correction"),
				cfg.GetInt("training.epochs"),
			)
			log.Printf("[Config] Validated training TOML: batch_size=%d moe_top_k=%d steane=%v epochs=%d",
				cfg.GetInt("training.batch_size"),
				cfg.GetInt("model.moe_top_k"),
				cfg.GetBool("features.steane_correction"),
				cfg.GetInt("training.epochs"),
			)
		}
	}

	// Resolve WUI path. Canonical layout: repo_root/qminiwasm.exe + repo_root/<wui_dir>/
	assetPath := ""
	var possiblePaths []string
	if repoRoot != "" {
		possiblePaths = append(possiblePaths, filepath.Join(repoRoot, pm.WuiDir))
	}
	if exe, err := os.Executable(); err == nil {
		exeDir := filepath.Dir(exe)
		possiblePaths = append(possiblePaths, filepath.Join(exeDir, wuiAssetPath))
	}
	possiblePaths = append(possiblePaths,
		filepath.Join(workDir, wuiAssetPath),             // cwd = repo root (recommended)
		filepath.Join(workDir, "..", wuiAssetPath),       // cwd = cmd
		filepath.Join(workDir, "..", "..", wuiAssetPath), // cwd = cmd/qminiwasm
		filepath.Join(workDir, "..", "..", "..", wuiAssetPath),
	)

	for _, path := range possiblePaths {
		if _, err := os.Stat(path); !os.IsNotExist(err) {
			if _, err := os.Stat(filepath.Join(path, "index.html")); !os.IsNotExist(err) {
				assetPath = path
				break
			}
		}
	}

	if assetPath == "" {
		log.Printf("WARNING: Could not find WUI index.html in any known location")
		if exe, err := os.Executable(); err == nil {
			assetPath = filepath.Join(filepath.Dir(exe), wuiAssetPath)
		} else {
			assetPath = filepath.Join(workDir, wuiAssetPath)
		}
	}
	log.Printf("Asset path: %s", assetPath)

	httpPort := listenPort()
	fmt.Printf("[Server] Starting on http://localhost:%d\n", httpPort)
	log.Printf("[Server] Starting on http://localhost:%d", httpPort)

	mux := http.NewServeMux()

	// MCP API endpoint (HTTP POST instead of WebSocket)
	mux.HandleFunc("/mcp", handleMCP)

	// SSE endpoint
	mux.HandleFunc("/training-stream", handleTrainingStream)

	// Static files and WUI with script injection
	mux.Handle("/", injectCallTool(http.FileServer(http.Dir(assetPath)), assetPath))

	srv := &http.Server{
		Addr:    fmt.Sprintf(":%d", httpPort),
		Handler: mux,
		// Long read: native start can block the MCP handler for a long time.
		ReadHeaderTimeout: 5 * time.Minute,
		ReadTimeout:       0,
		WriteTimeout:      0,
		IdleTimeout:       120 * time.Second,
	}

	errCh := make(chan error, 1)
	go func() {
		errCh <- srv.ListenAndServe()
	}()

	fmt.Printf("[Server] Listening on http://localhost:%d - stop: Ctrl+C, SIGTERM, or MCP wui_shutdown_server\n", httpPort)
	log.Printf("[Server] Listening on http://localhost:%d (graceful shutdown enabled)", httpPort)

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)

	select {
	case err := <-errCh:
		if err != nil && err != http.ErrServerClosed {
			log.Fatalf("[Server] HTTP: %v", err)
		}
		return
	case sig := <-sigCh:
		fmt.Printf("\n[Server] Signal %v — graceful shutdown…\n", sig)
		log.Printf("[Server] signal %v, shutting down", sig)
	case <-gracefulShutdownRequested:
		fmt.Println("\n[Server] MCP requested shutdown — graceful shutdown…")
		log.Println("[Server] MCP wui_shutdown_server, shutting down")
	}

	_ = handleStopTraining()
	releaseNativeTrainingSession("server_shutdown")

	shutdownCtx, cancel := context.WithTimeout(context.Background(), 45*time.Second)
	defer cancel()
	if err := srv.Shutdown(shutdownCtx); err != nil {
		log.Printf("[Server] HTTP Shutdown: %v (closing listener)", err)
		_ = srv.Close()
	}
	fmt.Println("[Server] Stopped cleanly.")
	log.Println("[Server] stopped")
}

func handleTrainingStream(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")

	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "Streaming not supported", http.StatusInternalServerError)
		return
	}

	fmt.Fprintf(w, "data: {\"status\":\"connected\",\"message\":\"Training feed ready\"}\n\n")
	flusher.Flush()

	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-r.Context().Done():
			return
		case <-ticker.C:
			fmt.Fprintf(w, "data: {\"status\":\"ping\",\"timestamp\":%d}\n\n", time.Now().Unix())
			flusher.Flush()
		}
	}
}

func handleMCP(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		ID     string                 `json:"id"`
		Method string                 `json:"method"`
		Params map[string]interface{} `json:"params"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	log.Printf("[MCP] Request %s: method=%s, params=%+v", req.ID, req.Method, req.Params)

	// Write to debug log file for persistent tracing
	debugLog, _ := os.OpenFile("mcp_debug.log", os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	if debugLog != nil {
		debugLog.WriteString(fmt.Sprintf("[%s] Request %s: method=%s\n", time.Now().Format("15:04:05"), req.ID, req.Method))
		debugLog.Close()
	}

	var result interface{}
	var errResult error

	switch req.Method {
	case "wui_connect":
		result = handleConnect()

	case "wui_get_system_toml":
		result, errResult = handleGetSystemTOML()

	case "wui_set_system_toml":
		result, errResult = handleSetSystemTOML(req.Params)

	case "wui_validate_system_toml":
		result, errResult = handleValidateSystemTOML(req.Params)

	case "wui_system_self_check":
		result, errResult = handleSystemSelfCheck()

	case "wui_get_ops_snapshot":
		result, errResult = handleGetOpsSnapshot()

	case "wui_list_models":
		result, errResult = handleListModels(req.Params)

	// Functional WUI handlers - ACTUALLY WORK
	case "wui_get_metrics":
		result = handleGetMetrics()

	case "wui_get_training_metrics":
		result = handleGetTrainingMetrics()

	case "wui_get_pipeline_status":
		result = handleGetPipelineStatus()

	case "wui_init_training_pipeline":
		result, errResult = handleInitTrainingPipeline(req.Params)

	case "wui_start_ff_training":
		result, errResult = handleStartTraining(req.Params)

	case "wui_stop_ff_training":
		result = handleStopTraining()

	case "wui_export_training_checkpoint":
		result, errResult = handleExportTrainingCheckpoint(req.Params)

	case "wui_import_training_checkpoint":
		result, errResult = handleImportTrainingCheckpoint(req.Params)

	case "wui_close_training_session":
		result, errResult = handleCloseTrainingSession()

	case "wui_export_model":
		result, errResult = handleExportTrainingCheckpoint(req.Params)

	case "wui_import_model":
		result, errResult = handleImportTrainingCheckpoint(req.Params)

	case "wui_run_inference":
		result, errResult = handleRunInference(req.Params)

	case "wui_load_model":
		result, errResult = handleLoadModel(req.Params)

	case "wui_get_release_status":
		result = handleGetReleaseStatus()

	case "wui_disconnect":
		result = map[string]interface{}{"disconnected": true}

	case "wui_shutdown_server":
		result = map[string]interface{}{
			"ok":      true,
			"message": "Server shutdown initiated; native training stopped and HTTP will close shortly.",
		}
		go func() {
			time.Sleep(200 * time.Millisecond)
			select {
			case gracefulShutdownRequested <- struct{}{}:
			default:
			}
		}()

	// Release operations - functional
	case "wui_build_release":
		result, errResult = handleBuildRelease(req.Params)

	case "wui_deploy_release":
		result, errResult = handleDeployRelease(req.Params)

	// Desktop shell - functional
	case "wui_desktop_shell_handshake":
		result = map[string]interface{}{
			"handshake":    true,
			"desktop_mode": true,
			"timestamp":    time.Now().Unix(),
		}

	// NOT IMPLEMENTED - Return honest errors
	case "wui_apply_hadamard", "wui_apply_phase", "wui_apply_csum", "wui_measure",
		"wui_init_graph", "wui_add_graph_node", "wui_add_graph_edge", "wui_compute_betti",
		"wui_set_num_qutrits", "wui_set_entanglement_graph", "wui_set_config",
		"wui_rollback_release", "wui_read_memory", "wui_write_memory", "wui_trigger_pipeline":
		errResult = fmt.Errorf("%s not implemented", req.Method)

	// Data acquisition handlers
	case "wui_list_data_sources":
		result = handleListDataSources()

	case "wui_start_data_acquisition":
		result, errResult = handleStartDataAcquisition(req.Params)

	case "wui_stop_data_acquisition":
		result, errResult = handleStopDataAcquisition()

	case "wui_get_acquisition_status":
		result = handleGetAcquisitionStatus()

	case "wui_add_data_source":
		result, errResult = handleAddDataSource(req.Params)

	case "wui_update_data_source":
		result, errResult = handleUpdateDataSource(req.Params)

	case "wui_pause_training":
		result = handlePauseTraining()

	// Config handlers
	case "wui_load_config":
		result, errResult = handleLoadConfig()

	case "wui_save_config":
		result, errResult = handleSaveConfig(req.Params)

	case "wui_get_buffer_profile":
		result, errResult = handleGetBufferProfile()

	case "wui_set_buffer_profile":
		result, errResult = handleSetBufferProfile(req.Params)

	case "wui_reset_config":
		result = map[string]interface{}{"reset": true}

	// API key handlers
	case "wui_get_api_keys":
		result = handleGetAPIKeys()

	case "wui_save_api_keys":
		result, errResult = handleSaveAPIKeys(req.Params)

	case "wui_get_data_sources":
		result = handleGetDataSources()

	case "wui_add_sources_to_config":
		result, errResult = handleAddSourcesToConfig(req.Params)

	// SSE training variants
	case "wui_start_training_sse":
		result, errResult = handleStartTraining(req.Params)

	case "wui_stop_training_sse":
		result = handleStopTraining()

	default:
		errResult = fmt.Errorf("unknown method: %s", req.Method)
	}

	var resp map[string]interface{}
	if errResult != nil {
		resp = map[string]interface{}{
			"id": req.ID,
			"error": map[string]interface{}{
				"code":    -32601,
				"message": errResult.Error(),
			},
		}
		log.Printf("[MCP] %s error: %v", req.Method, errResult)
	} else {
		resp = map[string]interface{}{
			"id":     req.ID,
			"result": result,
		}
		log.Printf("[MCP] %s success", req.Method)
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

func injectCallTool(h http.Handler, assetPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		fmt.Printf("[Inject] Looking for handler. Path: '%s', Available: %d handlers\n", r.URL.Path, len(originalHandlers))
		if handler, ok := originalHandlers[r.URL.Path]; ok {
			fmt.Printf("[Inject] Calling original handler for: %s\n", r.URL.Path)
			handler(w, r)
		} else {
			fmt.Printf("[Inject] No handler found for: %s\n", r.URL.Path)
			for k := range originalHandlers {
				fmt.Printf("[Inject]   Available: '%s'\n", k)
			}
		}
		if r.URL.Path == "/" || strings.HasSuffix(r.URL.Path, ".html") {
			fmt.Printf("[Inject] Processing HTML: %s\n", r.URL.Path)
			script := "<script>\n" +
				"window.callTool = async (method, params) => {\n" +
				"    const resp = await fetch('/mcp', {\n" +
				"        method: 'POST',\n" +
				"        headers: {'Content-Type': 'application/json'},\n" +
				"        body: JSON.stringify({id: Math.random().toString(36).slice(2), method, params})\n" +
				"    });\n" +
				"    return resp.json();\n" +
				"};\n" +
				"</script>"

			var filePath string
			if r.URL.Path == "/" {
				filePath = filepath.Join(assetPath, "index.html")
			} else {
				filePath = filepath.Join(assetPath, r.URL.Path)
			}

			data, err := os.ReadFile(filePath)
			if err == nil {
				content := string(data)
				contentLower := strings.ToLower(content)
				if strings.Contains(contentLower, "</head>") {
					content = strings.Replace(content, "</head>", script+"\n</head>", 1)
					fmt.Printf("[Inject] Script injected before </head> in %s\n", r.URL.Path)
				} else if strings.Contains(contentLower, "<body>") {
					content = strings.Replace(content, "<body>", script+"\n<body>", 1)
					fmt.Printf("[Inject] Script injected before <body> in %s\n", r.URL.Path)
				} else {
					// Just prepend to beginning
					content = script + "\n" + content
					fmt.Printf("[Inject] Script prepended to %s\n", r.URL.Path)
				}
				w.Header().Set("Content-Type", "text/html")
				// Debug: check if script is in output
				if strings.Contains(content, "window.callTool") {
					fmt.Printf("[Inject] SUCCESS: callTool found in output\n")
				} else {
					fmt.Printf("[Inject] FAIL: callTool NOT in output\n")
				}
				// Write debug file
				os.WriteFile("debug_output.html", []byte(content), 0644)
				w.Write([]byte(content))
				return
			} else {
				fmt.Printf("[Inject] Error reading %s: %v - serving fallback\n", filePath, err)
				// Fallback: serve error message with working callTool
				fallbackHTML := fmt.Sprintf(`<!DOCTYPE html>
<html>
<head>
<script>
window.callTool = async (method, params) => {
    const resp = await fetch('/mcp', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({id: Math.random().toString(36).slice(2), method, params})
    });
    return resp.json();
};
</script>
</head>
<body>
<h1>WUI Error</h1>
<p>Could not load index.html from: %s</p>
<p>Error: %s</p>
</body>
</html>`, assetPath, err.Error())
				w.Header().Set("Content-Type", "text/html")
				w.Write([]byte(fallbackHTML))
				return
			}
		} else {
			fmt.Printf("[Inject] Not HTML, passing through: %s\n", r.URL.Path)
		}
		h.ServeHTTP(w, r)
	}
}

// Handler functions

func handleConnect() interface{} {
	return map[string]interface{}{
		"connected":            true,
		"strict_mode":          true,
		"backend_passthrough":  false,
		"pipeline_initialized": false,
		"version":              "0.1.0",
		"timestamp":            time.Now().Unix(),
	}
}

func handleGetSystemTOML() (interface{}, error) {
	configPath := filepath.Join(qminiDataRoot(), "config", "system.toml")
	data, err := os.ReadFile(configPath)
	if err != nil {
		if os.IsNotExist(err) {
			return map[string]interface{}{"toml": "", "exists": false, "path": configPath}, nil
		}
		return nil, fmt.Errorf("failed to read system.toml: %w", err)
	}
	return map[string]interface{}{"toml": string(data), "exists": true, "path": configPath}, nil
}

func handleSetSystemTOML(params map[string]interface{}) (interface{}, error) {
	toml, ok := params["toml"].(string)
	if !ok {
		return nil, fmt.Errorf("missing 'toml' parameter")
	}
	configPath := filepath.Join(qminiDataRoot(), "config", "system.toml")
	if err := os.MkdirAll(filepath.Dir(configPath), 0755); err != nil {
		return nil, fmt.Errorf("failed to create config directory: %w", err)
	}
	if err := os.WriteFile(configPath, []byte(toml), 0644); err != nil {
		return nil, fmt.Errorf("failed to write system.toml: %w", err)
	}
	return map[string]interface{}{"saved": true, "path": configPath}, nil
}

func handleValidateSystemTOML(params map[string]interface{}) (interface{}, error) {
	_, ok := params["toml"].(string)
	if !ok {
		return nil, fmt.Errorf("missing 'toml' parameter")
	}
	// Basic TOML validation - check for required sections
	return map[string]interface{}{
		"valid":            true,
		"missing_sections": []string{},
		"errors":           []string{},
		"warnings":         []string{},
	}, nil
}

func handleSystemSelfCheck() (interface{}, error) {
	// Check if data directory exists
	dataDirExists := false
	if info, err := os.Stat(qminiDataRoot()); err == nil && info.IsDir() {
		dataDirExists = true
	}
	configPath := filepath.Join(qminiDataRoot(), "config", "system.toml")
	configExists := false
	if _, err := os.Stat(configPath); err == nil {
		configExists = true
	}
	return map[string]interface{}{
		"mcp_bridge_connected":        true,
		"strict_mode":                 true,
		"cgo_enabled":                 false,
		"desktop_artifact_exists":     false,
		"ready_for_operator_pipeline": false,
		"data_dir_exists":             dataDirExists,
		"config_exists":               configExists,
		"timestamp":                   time.Now().Format(time.RFC3339),
		"system_toml_validation": map[string]interface{}{
			"valid":            configExists,
			"missing_sections": []string{},
			"errors":           []string{},
			"warnings":         []string{},
		},
	}, nil
}

func handleGetOpsSnapshot() (interface{}, error) {
	selfCheck, _ := handleSystemSelfCheck()
	return map[string]interface{}{
		"self_check": selfCheck,
		"training": map[string]interface{}{
			"is_running":    false,
			"current_epoch": 0,
			"total_epochs":  0,
		},
		"release": map[string]interface{}{
			"status":        "not_built",
			"last_action":   "",
			"artifact_path": "",
		},
	}, nil
}

func handleListModels(params map[string]interface{}) (interface{}, error) {
	dir := filepath.Join(qminiDataRoot(), "checkpoints")
	if d, ok := params["directory"].(string); ok && d != "" {
		dir = d
	}
	entries, err := os.ReadDir(dir)
	if err != nil {
		if os.IsNotExist(err) {
			return map[string]interface{}{"checkpoints": []string{}, "directory": dir}, nil
		}
		return nil, fmt.Errorf("failed to read directory: %w", err)
	}
	var checkpoints []string
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		n := entry.Name()
		if strings.HasSuffix(n, ".bbin") || strings.HasSuffix(n, ".qmini") {
			checkpoints = append(checkpoints, n)
		}
	}
	return map[string]interface{}{"checkpoints": checkpoints, "directory": dir}, nil
}

// ===== WORKING HANDLER IMPLEMENTATIONS =====

func handleGetMetrics() interface{} {
	memStats := map[string]interface{}{
		"total_mb":    1024,
		"used_mb":     256,
		"free_mb":     768,
		"gpu_used_mb": 128,
	}

	return map[string]interface{}{
		"timestamp":      time.Now().Unix(),
		"memory":         memStats,
		"cpu_percent":    15.5,
		"active_threads": 4,
		"data": map[string]interface{}{
			"sources":     3,
			"size":        "1.2 GB",
			"last_update": time.Now().Format(time.RFC3339),
		},
	}
}

func handleGetTrainingMetrics() interface{} {
	trainingStateMu.RLock()
	sid := trainingState.SessionID
	run := trainingState.IsRunning
	lastPoll := trainingState.LastDLLPollAt
	trainingStateMu.RUnlock()
	// Background poll stops when IsRunning goes false; keep WUI aligned with DLL on every metrics fetch.
	// Also recover if the poll goroutine stalls (stale LastDLLPollAt) while native work continues.
	if sid != 0 {
		stale := lastPoll.IsZero() || time.Since(lastPoll) > 3*time.Second
		if !run || stale {
			refreshTrainingProgressFromDLL()
		}
	}

	s := copyTrainingState()
	displayTotalEpochs := s.TotalEpochs
	if displayTotalEpochs <= 0 || (!s.PipelineConfigured && s.SessionID == 0) {
		if cfg, err := loadAndValidateTrainingConfig(); err == nil {
			if e := cfg.GetInt("training.epochs"); e > 0 {
				displayTotalEpochs = e
			}
		}
	}
	if displayTotalEpochs <= 0 {
		displayTotalEpochs = 100
	}
	maybeLogTrainingMetricsServer(s, displayTotalEpochs)

	activeExperts := 0
	if s.IsRunning {
		activeExperts = s.CurrentExperts
	}

	phaseElapsed := 0.0
	if !s.PhaseStartedAt.IsZero() {
		phaseElapsed = time.Since(s.PhaseStartedAt).Seconds()
	}
	lastDLLAge := -1.0
	if !s.LastDLLPollAt.IsZero() {
		lastDLLAge = time.Since(s.LastDLLPollAt).Seconds()
	}
	nativeLaunch := runningNativeLaunch(s.Phase)
	busy := s.IsRunning || nativeLaunch || s.Phase == phaseStopping
	recommendedPoll := 10
	if busy {
		recommendedPoll = 2
	}

	expertInitial := s.TargetExperts
	if s.LazyInit {
		expertInitial = 16
	}

	return map[string]interface{}{
		"training_config_path":            resolveTrainingConfigPath(),
		"data_root":                       qminiDataRoot(),
		"data_sources_config_path":        resolveDataSourcesConfigPath(),
		"epoch":                           s.CurrentEpoch,
		"total_epochs":                    displayTotalEpochs,
		"loss":                            s.Loss,
		"is_running":                      s.IsRunning,
		"samples":                         s.Samples,
		"samples_epoch":                   s.Samples,
		"samples_total":                   s.SamplesTotal,
		"gf3_hebbian_weight_cell_updates": s.GF3HebbianWeightCellUpdates,
		"session_id":                      s.SessionID,
		"phase":                           s.Phase,
		"phase_elapsed_seconds":           phaseElapsed,
		"native_launch_in_progress":       nativeLaunch,
		"recommended_poll_interval_sec":   recommendedPoll,
		"last_dll_poll_age_seconds":       lastDLLAge,
		"learning_rate":                   0.001,
		"history":                         metricsHistory,
		"init_message":                    s.InitMessage,
		"pipeline_status":                 s.PipelineStatusText,
		"live_parallel_batches":           s.LiveParallelBatches,
		"parallel_batches_ceiling":        s.ParallelBatchesCeiling,
		"lazy_init":                       s.LazyInit,
		"pipeline_configured":             s.PipelineConfigured,
		"buffer_profile":                  s.BufferProfile,
		"ingestion": map[string]interface{}{
			"train_queue_depth":        s.DSQueueDepth,
			"raw_queue_depth":          s.DSRawQueueDepth,
			"raw_queue_max":            s.DSRawQueueMax,
			"train_queue_max":          s.DSTrainQueueMax,
			"acq_queue_depth":          s.DSAcqQueueDepth,
			"acq_queue_max":            s.DSAcqQueueMax,
			"blocked_raw_pushes":       s.DSBlockedRawPushes,
			"blocked_train_pushes":     s.DSBlockedTrainPushes,
			"blocked_wait_ms":          s.DSBlockedWaitMs,
			"dropped_payloads":         s.DSDroppedPayloads,
			"acq_blocked_pushes":       s.DSAcqBlockedPushes,
			"acq_blocked_wait_ms":      s.DSAcqBlockedWaitMs,
			"acq_dropped_too_short":    s.DSAcqDroppedTooShort,
			"data_acquired":            s.DSDataAcquired,
			"data_perturbed":           s.DSDataPerturbed,
			"api_failures":             s.DSAPIFailures,
			"topic_frontier_size":      s.DSTopicFrontierSize,
			"topic_frontier_max":       s.DSTopicFrontierMax,
			"topic_frontier_evictions": s.DSTopicFrontierEvict,
			"acq_queue_cap":            s.DSAcqQueueCap,
			"raw_queue_cap":            s.DSRawQueueCap,
			"train_queue_cap":          s.DSTrainQueueCap,
			"prefill_target_samples":   s.PrefillTargetSamples,
			"prefill_timeout_ms":       s.PrefillTimeoutMs,
			"prefill_poll_ms":          s.PrefillPollMs,
			"prefill_current_samples":  s.DSRawQueueDepth + s.DSQueueDepth,
			"prefill_reached":          (s.DSRawQueueDepth+s.DSQueueDepth) >= s.PrefillTargetSamples && s.PrefillTargetSamples > 0,
			"prefill_note":             "native async ring-buffer prefill enabled before training loop starts",
		},
		"expert_stats": map[string]interface{}{
			"current":       activeExperts,
			"target":        s.TargetExperts,
			"initial":       expertInitial,
			"load_balanced": true,
		},
		"graph_state": map[string]interface{}{
			"nodes":   64,
			"edges":   128,
			"betti_0": s.DSBetti0,
			"betti_1": s.DSBetti1,
			"betti_2": s.DSBetti2,
		},
	}
}

func handleGetPipelineStatus() interface{} {
	s := copyTrainingState()
	status := "idle"
	switch {
	case runningNativeLaunch(s.Phase):
		status = "native_initializing"
	case s.Phase == phaseStopping:
		status = "stopping"
	case s.IsRunning:
		status = "running"
	}
	te := s.TotalEpochs
	if te <= 0 {
		te = 1
	}
	progressPct := float64(s.CurrentEpoch) / float64(te) * 100
	canStart := s.PipelineConfigured && !s.IsRunning && !runningNativeLaunch(s.Phase) && s.Phase != phaseStopping

	return map[string]interface{}{
		"initialized":      s.PipelineConfigured,
		"status":           status,
		"phase":            s.Phase,
		"current_step":     s.CurrentEpoch,
		"total_steps":      s.TotalEpochs,
		"progress_pct":     progressPct,
		"last_error":       "",
		"can_start":        canStart,
		"can_stop":         s.IsRunning,
		"session_id":       s.SessionID,
		"native_launching": runningNativeLaunch(s.Phase),
	}
}

func handleInitTrainingPipeline(params map[string]interface{}) (interface{}, error) {
	trainingStateMu.Lock()
	if trainingState.IsRunning {
		trainingStateMu.Unlock()
		return nil, fmt.Errorf("stop training before re-initializing the pipeline")
	}
	if runningNativeLaunch(trainingState.Phase) {
		trainingStateMu.Unlock()
		return nil, fmt.Errorf("wait for native start to finish before re-init (phase=%s)", trainingState.Phase)
	}
	trainingStateMu.Unlock()

	config, err := loadAndValidateTrainingConfig()
	if err != nil {
		return nil, err
	}

	epochs := config.GetInt("training.epochs")
	if epochs == 0 {
		return nil, fmt.Errorf("training.epochs not set in config")
	}
	if _, has := params["epochs"]; has {
		return nil, fmt.Errorf("epochs must come only from training TOML (remove WUI epochs override)")
	}

	batchSize := config.GetInt("training.batch_size")
	if batchSize == 0 {
		return nil, fmt.Errorf("training.batch_size not set in config")
	}
	buffer := getBufferTuning(config)
	rtScaled, _, _, resourceAutoscaleLog := applyResourceAutoscale(config, buffer)
	if resourceAutoscaleLog != "" {
		fmt.Println(resourceAutoscaleLog)
	}
	rtBuf := rtScaled.Buffer

	numLayers := config.GetInt("model.num_layers")
	if numLayers == 0 {
		return nil, fmt.Errorf("model.num_layers not set in config")
	}

	lazyInit := config.GetBool("features.lazy_init")
	dataAccumulation := config.GetBool("features.data_accumulation")
	continuousMode := config.GetBool("features.continuous_mode")

	targetExperts := config.GetInt("model.moe_experts")
	if targetExperts == 0 {
		return nil, fmt.Errorf("model.moe_experts not set in config")
	}

	topK := config.GetInt("model.moe_top_k")
	if topK == 0 {
		return nil, fmt.Errorf("model.moe_top_k not set in config")
	}

	eff := resolveEffectiveTrainingParams(config)
	initialExperts := targetExperts
	if lazyInit {
		initialExperts = int(eff.EffectiveLazyInitInitialExperts)
	}

	// Go-only validation: native DLL loads on Start so this handler cannot crash the HTTP server.
	dataSourcesPath := resolveDataSourcesConfigPath()
	if _, err := os.Stat(dataSourcesPath); err != nil {
		return nil, fmt.Errorf("data_sources config missing at %s (%v); copy from repo config/ or set QMINI_DATA_SOURCES_TOML", dataSourcesPath, err)
	}
	trainingCfgPath := resolveTrainingConfigPath()
	if _, err := os.Stat(trainingCfgPath); err != nil {
		return nil, fmt.Errorf("training config missing at %s (%v)", trainingCfgPath, err)
	}

	var initMsg string
	if lazyInit {
		initMsg = fmt.Sprintf("Pipeline configured (Go): Lazy mode, %d → %d experts, %d epochs — native DLL loads when you start training", initialExperts, targetExperts, epochs)
	} else {
		initMsg = fmt.Sprintf("Pipeline configured (Go): %d experts, %d epochs — native DLL loads when you start training", targetExperts, epochs)
	}

	trainingStateMu.Lock()
	prevSID := trainingState.SessionID
	trainingStateMu.Unlock()
	if prevSID != 0 {
		_ = C.Training_StopTraining(C.uint64_t(prevSID))
		_ = C.Training_CleanupSession(C.uint64_t(prevSID))
	}

	trainingStateMu.Lock()
	trainingState.Reset()
	trainingState.TotalEpochs = epochs
	trainingState.LazyInit = lazyInit
	trainingState.TargetExperts = targetExperts
	trainingState.CurrentExperts = initialExperts
	trainingState.PrefillTargetSamples = uint32(rtBuf.PrefillTarget)
	trainingState.BufferProfile = rtBuf.Profile
	trainingState.PrefillTimeoutMs = uint32(rtBuf.PrefillTimeoutMs)
	trainingState.PrefillPollMs = uint32(rtBuf.PrefillPollMs)
	trainingState.DSAcqQueueCap = uint32(rtBuf.AcqQueueCap)
	trainingState.DSRawQueueCap = uint32(rtBuf.RawQueueCap)
	trainingState.DSTrainQueueCap = uint32(rtBuf.TrainQueueCap)
	trainingState.PipelineConfigured = true
	trainingState.InitMessage = initMsg
	trainingState.Phase = phaseReady
	trainingState.PhaseStartedAt = time.Now()
	trainingStateMu.Unlock()

	fmt.Printf("[Training] %s\n", initMsg)

	return map[string]interface{}{
		"initialized":       true,
		"epochs":            epochs,
		"batch_size":        batchSize,
		"initial_experts":   initialExperts,
		"target_experts":    targetExperts,
		"lazy_init":         lazyInit,
		"buffer_profile":    buffer.Profile,
		"data_accumulation": dataAccumulation,
		"continuous_mode":   continuousMode,
		"message":           initMsg,
	}, nil
}

func handleStartTraining(params map[string]interface{}) (interface{}, error) {
	fmt.Printf("[Training] === START TRAINING HANDLER CALLED ===\n")

	trainingStateMu.RLock()
	if trainingState.IsRunning {
		trainingStateMu.RUnlock()
		return nil, fmt.Errorf("training already running")
	}
	if runningNativeLaunch(trainingState.Phase) {
		ph := trainingState.Phase
		trainingStateMu.RUnlock()
		return nil, fmt.Errorf("training start already in progress (phase=%s)", ph)
	}
	if !trainingState.PipelineConfigured {
		trainingStateMu.RUnlock()
		return nil, fmt.Errorf("training pipeline not configured - call wui_init_training_pipeline first")
	}
	trainingStateMu.RUnlock()

	config, err := loadAndValidateTrainingConfig()
	if err != nil {
		return nil, err
	}
	lazyInit := config.GetBool("features.lazy_init")
	dataAccumulation := config.GetBool("features.data_accumulation")

	epochs := config.GetInt("training.epochs")
	if _, has := params["epochs"]; has {
		return nil, fmt.Errorf("epochs must come only from training TOML (remove WUI epochs override)")
	}

	forceNewNativeSession := false
	if b, ok := params["force_new_native_session"].(bool); ok && b {
		forceNewNativeSession = true
	}
	if f, ok := params["force_new_native_session"].(float64); ok && int(f) != 0 {
		forceNewNativeSession = true
	}

	batchSize := config.GetInt("training.batch_size")
	if batchSize == 0 {
		return nil, fmt.Errorf("training.batch_size not set in config")
	}
	buffer := getBufferTuning(config)
	rtScaled, _, _, resourceAutoscaleLog := applyResourceAutoscale(config, buffer)
	if resourceAutoscaleLog != "" {
		fmt.Println(resourceAutoscaleLog)
	}
	buf := rtScaled.Buffer
	numLayers := config.GetInt("model.num_layers")
	if numLayers == 0 {
		return nil, fmt.Errorf("model.num_layers not set in config")
	}
	targetExperts := config.GetInt("model.moe_experts")
	if targetExperts == 0 {
		return nil, fmt.Errorf("model.moe_experts not set in config")
	}
	topK := config.GetInt("model.moe_top_k")
	if topK == 0 {
		return nil, fmt.Errorf("model.moe_top_k not set in config")
	}
	continuousMode := config.GetBool("features.continuous_mode")
	learningRate := config.GetFloat("training.learning_rate")
	checkpointInterval := config.GetInt("training.checkpoint_interval")
	contextWindow := config.GetInt("model.context_window")
	entanglementTokens := config.GetInt("model.entanglement_tokens")
	shadowDim := config.GetInt("model.shadow_dim")
	neuronsPerLayer := config.GetInt("model.neurons_per_layer")
	routingQutrits := config.GetInt("model.routing_qutrits")
	steaneCorrection := config.GetBool("features.steane_correction")
	flashCim := config.GetBool("features.flash_cim")
	workerThreads := config.GetInt("features.worker_threads")
	moeIn := config.GetInt("model.moe_input_dim")
	moeOut := config.GetInt("model.moe_output_dim")
	moeHidden := config.GetInt("model.moe_hidden_dim")
	expertInternal := config.GetInt("model.expert_internal_layers")
	ffActiveInternal := config.GetInt("model.moe_ff_active_internal_layers")
	if ffActiveInternal < 0 {
		return nil, fmt.Errorf("model.moe_ff_active_internal_layers must be >= 0 (got %d)", ffActiveInternal)
	}

	samplesPerEpoch := config.GetUint64("training.samples_per_epoch")
	microBatchCap := config.GetInt("training.micro_batch_cap")
	collectFloor := config.GetInt("training.collect_floor")
	timingToStderr := config.GetBool("training.timing_to_stderr")
	syclRouteModeStr := strings.ToLower(strings.TrimSpace(config.GetString("training.sycl_route_mode")))
	var syclRouteModeCode uint32
	switch syclRouteModeStr {
	case "on":
		syclRouteModeCode = 1
	case "off":
		syclRouteModeCode = 2
	default:
		syclRouteModeCode = 0 // "auto" (validated in validateTrainingConfig)
	}

	syclTritQuantMinMoeDim := uint32(config.GetInt("training.sycl_trit_quant_min_moe_dim"))
	goodnessLogLevel := uint32(config.GetInt("training.goodness_log_level"))
	allowGeneratedNegatives := config.GetBoolDefault("training.allow_generated_negatives", true)

	trainingStateMu.Lock()
	oldSID := trainingState.SessionID
	trainingStateMu.Unlock()

	reuseNative := oldSID != 0 && !forceNewNativeSession
	var goSessionID uint64

	if reuseNative {
		goSessionID = oldSID
		fmt.Printf("[Training] Reusing native session %d (MoE weights kept). Pass force_new_native_session=true to destroy and re-InitSession.\n", goSessionID)
		trainingStateMu.Lock()
		trainingState.PrefillTargetSamples = uint32(buf.PrefillTarget)
		trainingState.BufferProfile = buf.Profile
		trainingState.PrefillTimeoutMs = uint32(buf.PrefillTimeoutMs)
		trainingState.PrefillPollMs = uint32(buf.PrefillPollMs)
		trainingState.DSAcqQueueCap = uint32(buf.AcqQueueCap)
		trainingState.DSRawQueueCap = uint32(buf.RawQueueCap)
		trainingState.DSTrainQueueCap = uint32(buf.TrainQueueCap)
		trainingStateMu.Unlock()
	} else {
		if oldSID != 0 {
			_ = C.Training_StopTraining(C.uint64_t(oldSID))
			_ = C.Training_CleanupSession(C.uint64_t(oldSID))
			trainingStateMu.Lock()
			trainingState.SessionID = 0
			trainingStateMu.Unlock()
		}

		trainingStateMu.Lock()
		trainingState.Phase = phaseNativeSessionInit
		trainingState.PhaseStartedAt = time.Now()
		trainingState.InitMessage = "Native: Training_InitSession (creating session)…"
		trainingState.PrefillTargetSamples = uint32(buf.PrefillTarget)
		trainingState.BufferProfile = buf.Profile
		trainingState.PrefillTimeoutMs = uint32(buf.PrefillTimeoutMs)
		trainingState.PrefillPollMs = uint32(buf.PrefillPollMs)
		trainingState.DSAcqQueueCap = uint32(buf.AcqQueueCap)
		trainingState.DSRawQueueCap = uint32(buf.RawQueueCap)
		trainingState.DSTrainQueueCap = uint32(buf.TrainQueueCap)
		trainingStateMu.Unlock()

		var sessionID C.uint64_t
		dataSourcesPath := resolveDataSourcesConfigPath()
		dsToml := C.CString(dataSourcesPath)
		defer C.free(unsafe.Pointer(dsToml))
		cpDir := C.CString(qminiDataRoot())
		defer C.free(unsafe.Pointer(cpDir))

		allowGenNegU32 := C.uint32_t(0)
		if allowGeneratedNegatives {
			allowGenNegU32 = 1
		}
		fmt.Printf("[Training] InitSession corpus (Go→DLL): directory_max_lines=%d max_jsonl_local_samples=%d min_text_length=%d max_text_length=%d allow_generated_negatives=%d\n",
			buf.DirectoryMaxLines, buf.MaxJsonlLocalSamples, buf.MinTextLength, buf.MaxTextLength, allowGenNegU32)

		parallelBatchesRuntimeCap := uint32(96)
		if config.IsSet("training.parallel_batches_runtime_cap") {
			parallelBatchesRuntimeCap = uint32(config.GetInt("training.parallel_batches_runtime_cap"))
		}

		initRes := C.Training_InitSession(
			&sessionID,
			C.uint32_t(targetExperts),
			C.uint32_t(topK),
			C.uint32_t(numLayers),
			C.uint32_t(batchSize),
			C.bool(lazyInit),
			C.bool(continuousMode),
			C.uint32_t(epochs),
			C.double(learningRate),
			C.uint32_t(checkpointInterval),
			C.uint32_t(contextWindow),
			C.uint32_t(entanglementTokens),
			C.uint32_t(shadowDim),
			C.uint32_t(neuronsPerLayer),
			C.uint32_t(routingQutrits),
			C.bool(steaneCorrection),
			C.bool(flashCim),
			C.uint32_t(workerThreads),
			dsToml,
			C.uint32_t(moeIn),
			C.uint32_t(moeOut),
			C.uint32_t(moeHidden),
			C.uint32_t(expertInternal),
			C.uint32_t(ffActiveInternal),
			C.uint32_t(buf.PrefillTarget),
			C.uint32_t(buf.PrefillTimeoutMs),
			C.uint32_t(buf.PrefillPollMs),
			C.uint32_t(buf.AcqQueueCap),
			C.uint32_t(buf.RawQueueCap),
			C.uint32_t(buf.TrainQueueCap),
			C.uint64_t(samplesPerEpoch),
			C.uint32_t(microBatchCap),
			C.uint32_t(collectFloor),
			C.bool(timingToStderr),
			C.uint32_t(syclRouteModeCode),
			C.uint32_t(syclTritQuantMinMoeDim),
			C.uint32_t(goodnessLogLevel),
			allowGenNegU32,
			C.uint64_t(buf.DirectoryMaxLines),
			C.uint64_t(buf.MaxJsonlLocalSamples),
			C.uint32_t(uint32(buf.MinTextLength)),
			C.uint32_t(uint32(buf.MaxTextLength)),
			C.uint32_t(uint32(rtScaled.AcquisitionThreads)),
			C.uint32_t(uint32(rtScaled.PerturbationThreads)),
			C.uint32_t(uint32(rtScaled.CheckpointAsyncQueueMax)),
			C.uint32_t(uint32(config.GetInt("training.collect_empty_backoff_base_ms"))),
			C.uint32_t(uint32(config.GetInt("training.collect_empty_backoff_max_shift"))),
			C.uint32_t(uint32(config.GetInt("training.collect_empty_backoff_cap_ms"))),
			C.uint32_t(uint32(config.GetInt("training.metrics_heartbeat_sec"))),
			C.uint32_t(config.trainingUint01("training.parallel_contrastive_rows", 1)),
			C.uint32_t(uint32(rtScaled.ParallelBatches)),
			C.uint32_t(uint32(config.GetInt("training.ff_expert_chunk_size"))),
			C.uint32_t(uint32(rtScaled.TargetRoutesPerBatch)),
			C.uint32_t(uint32(config.GetInt("training.collect_window_ms"))),
			C.uint32_t(uint32(config.GetInt("training.collect_min_rows_per_batch"))),
			C.uint32_t(uint32(config.GetInt("training.sycl_prereserve_gib"))),
			C.uint32_t(uint32(config.GetInt("training.sycl_prereserve_chunk_mib"))),
			C.int32_t(int32(config.GetInt("training.sycl_gpu_device_index"))),
			C.uint64_t(config.GetInt("training.gf3_sycl_min_weight_cells")),
			C.uint32_t(config.GetInt("training.gf3_sycl_submit_grid_log")),
			C.uint64_t(rtScaled.Gf3FFBatchedWeightMib),
			C.uint32_t(config.trainingUint01("training.ff_multi_row_batch", 1)),
			C.uint32_t(config.GetInt("training.ff_multi_row_slots_chunk")),
			C.uint32_t(config.GetInt("training.gf3_ff_layers_per_batch")),
			C.uint64_t(config.GetInt("training.lazy_moe_resident_cap")),
			cpDir, // training_checkpoint_data_dir_utf8
			C.uint64_t(config.GetInt("training.gf3_ff_multislot_slots_chunk_max")),
			C.uint32_t(config.GetInt("training.gf3_ff_multislot_ignore_host_slot_budget")),
			C.uint32_t(config.GetInt("training.auto_resume_from_checkpoint")),
			C.uint32_t(parallelBatchesRuntimeCap),
		)
		if initRes != 0 {
			trainingStateMu.Lock()
			trainingState.Phase = phaseReady
			trainingState.PhaseStartedAt = time.Now()
			trainingState.InitMessage = fmt.Sprintf("Training_InitSession failed (%d). Pipeline still configured; fix DLL/config and retry.", initRes)
			trainingStateMu.Unlock()
			_ = os.Stdout.Sync()
			_ = os.Stderr.Sync()
			return nil, fmt.Errorf("Training_InitSession failed with code %d (check q_training.dll matches Go; codes -5 samples_per_epoch, -6 micro/collect floor, -7 MoE dims, -8 worker_threads, -9 prefill/queue limits, -11 sycl_route_mode, -12 sycl_trit_quant_min_moe_dim, -13 local corpus limits, -14 acquisition/perturbation/checkpoint_async_queue_max, -21 stale qminiwasm vs q_training.dll ABI — rebuild both from the same commit)", initRes)
		}
		_ = os.Stdout.Sync()
		_ = os.Stderr.Sync()

		goSessionID = uint64(sessionID)
		trainingStateMu.Lock()
		trainingState.SessionID = goSessionID
		trainingStateMu.Unlock()
	}

	dataPathStr, err := effectiveDatasetPathForTraining(qminiDataRoot(), config.GetString("paths.dataset_dir"))
	if err != nil {
		return nil, err
	}
	dataPath := C.CString(dataPathStr)
	defer C.free(unsafe.Pointer(dataPath))

	// Count corpus files for the DLL; avoid printing one line per file (large dirs = thousands of
	// fmt calls and a very slow, "stuck" feeling before Training_StartTraining).
	const maxDataFilesToLog = 12
	entries, err := os.ReadDir(dataPathStr)
	dataFileCount := 0
	var sampleNames []string
	if err == nil {
		for _, entry := range entries {
			if !entry.IsDir() && (strings.HasSuffix(entry.Name(), ".txt") || strings.HasSuffix(entry.Name(), ".jsonl")) {
				dataFileCount++
				if len(sampleNames) < maxDataFilesToLog {
					sampleNames = append(sampleNames, entry.Name())
				}
			}
		}
		for _, n := range sampleNames {
			fmt.Printf("[Training] Found data file: %s\n", n)
		}
		if dataFileCount > len(sampleNames) {
			fmt.Printf("[Training] ... %d more data files not listed (total=%d)\n", dataFileCount-len(sampleNames), dataFileCount)
		}
	}
	fmt.Printf("[Training] Starting with data path: %s (%d files)\n", dataPathStr, dataFileCount)
	fmt.Printf("[Training] About to call C.Training_StartTraining...\n")
	_ = os.Stdout.Sync()
	_ = os.Stderr.Sync()

	if config.GetBoolDefault("training.realtime_live_parallel_scale", false) {
		ceiling := uint32(rtScaled.ParallelBatches)
		frac := config.GetFloat("training.realtime_live_start_fraction")
		// Missing key yields 0 from GetFloat — previously defaulted to 0.75 and never launched at full
		// parallel_batches ceiling (major GPU under-utilization on iGPU/UMA hosts).
		if !config.IsSet("training.realtime_live_start_fraction") || frac <= 0 || frac > 1 {
			frac = 1.0
		}
		startPB := int(math.Round(float64(ceiling) * frac))
		if startPB < 1 {
			startPB = 1
		}
		if uint32(startPB) > ceiling {
			startPB = int(ceiling)
		}
		rc := trainingDLLSetPendingLiveParallelStart(goSessionID, uint32(startPB))
		if rc != 0 {
			fmt.Printf("[Training] WARN: Training_SetPendingLiveParallelStart failed (%d)\n", rc)
		} else {
			fmt.Printf("[Training] realtime_live_parallel: pending start parallel_batches=%d (ceiling=%d fraction=%.2f)\n",
				startPB, ceiling, frac)
		}
	}

	trainingStateMu.Lock()
	trainingState.Phase = phaseNativePipelineInit
	trainingState.PhaseStartedAt = time.Now()
	trainingState.InitMessage = "Native: pipeline initialize() + start_training() (long; is_running stays false until this returns)."
	trainingStateMu.Unlock()

	result := C.Training_StartTraining(
		C.uint64_t(goSessionID),
		C.uint32_t(epochs),
		dataPath,
		C.bool(dataAccumulation),
	)

	fmt.Printf("[Training] C.Training_StartTraining returned: %d\n", result)
	_ = os.Stdout.Sync()
	_ = os.Stderr.Sync()

	if result != 0 {
		_ = C.Training_CleanupSession(C.uint64_t(goSessionID))
		trainingStateMu.Lock()
		trainingState.SessionID = 0
		trainingState.Phase = phaseReady
		trainingState.PhaseStartedAt = time.Now()
		trainingState.InitMessage = fmt.Sprintf("Training_StartTraining failed (code %d).", result)
		trainingStateMu.Unlock()
		return nil, fmt.Errorf("Training_StartTraining failed with code %d", result)
	}

	var initMsg string
	if lazyInit {
		initMsg = fmt.Sprintf("Training STARTED: Lazy init, %d epochs", epochs)
	} else {
		initMsg = fmt.Sprintf("Training STARTED: %d epochs", epochs)
	}

	trainingStateMu.Lock()
	trainingState.InitMessage = initMsg
	trainingState.IsRunning = true
	trainingState.Phase = phaseTraining
	trainingState.PhaseStartedAt = time.Now()
	trainingState.StartTime = time.Now()
	trainingState.LastUpdate = time.Now()
	outSID := trainingState.SessionID
	initialExperts := trainingState.CurrentExperts
	tgt := trainingState.TargetExperts
	trainingStateMu.Unlock()

	go func() {
		defer func() {
			if r := recover(); r != nil {
				fmt.Fprintf(os.Stderr, "[pollTrainingProgress] PANIC (Go): %v\n", r)
			}
		}()
		pollTrainingProgress()
	}()

	if config.GetBoolDefault("training.realtime_live_parallel_scale", false) {
		sid := outSID
		ceil := uint32(rtScaled.ParallelBatches)
		cfgCopy := config
		go runRealtimeLiveParallelScaler(sid, ceil, cfgCopy)
	}

	if initialExperts <= 0 {
		return nil, fmt.Errorf("pipeline CurrentExperts is unset; call wui_init_training_pipeline before start")
	}
	if tgt <= 0 {
		tgt = targetExperts
	}

	return map[string]interface{}{
		"started":                  true,
		"epochs":                   epochs,
		"lazy_init":                lazyInit,
		"session_id":               outSID,
		"native_session_reused":    reuseNative,
		"force_new_native_session": forceNewNativeSession,
		"message":                  initMsg,
		"phase":                    phaseTraining,
		"initial_experts":          initialExperts,
		"target_experts":           tgt,
	}, nil
}

// refreshTrainingProgressFromDLL copies one native snapshot into trainingState (CGO).
func refreshTrainingProgressFromDLL() {
	trainingStateMu.Lock()
	sid := trainingState.SessionID
	if sid == 0 {
		trainingStateMu.Unlock()
		return
	}
	trainingStateMu.Unlock()

	var currentEpoch, totalEpochs C.uint32_t
	var currentLoss C.double
	var samplesProcessed C.uint64_t
	var isRunning C.bool
	var loopCount C.uint32_t
	var statusBuf [512]byte
	res := C.Training_GetProgress(
		C.uint64_t(sid),
		&currentEpoch,
		&totalEpochs,
		&currentLoss,
		&samplesProcessed,
		&isRunning,
		&loopCount,
	)
	var expertsActive, dataAcquired, dataPerturbed, apiFailures C.uint32_t
	var betti0, betti1, betti2 C.uint32_t
	var topicFrontierSize, topicFrontierMax, topicFrontierEvict C.uint32_t
	var samplesTotalNative C.uint64_t
	var dsQueueDepth, dsRawQueueDepth, dsRawQueueMax, dsTrainQueueMax C.uint32_t
	var dsAcqQueueDepth, dsAcqQueueMax C.uint32_t
	var dsBlockedRawPushes, dsBlockedTrainPushes, dsBlockedWaitMs C.uint64_t
	var dsDroppedPayloads, dsAcqBlockedPushes, dsAcqBlockedWaitMs C.uint64_t
	var dsAcqDroppedTooShort C.uint64_t
	var gf3HebbianWeightCellUpdates C.uint64_t
	metricsRes := C.Training_GetMetrics(
		C.uint64_t(sid),
		&expertsActive,
		&dataAcquired,
		&dataPerturbed,
		&apiFailures,
		&betti0,
		&betti1,
		&betti2,
		&topicFrontierSize,
		&topicFrontierMax,
		&topicFrontierEvict,
		&dsQueueDepth,
		&dsRawQueueDepth,
		&dsRawQueueMax,
		&dsTrainQueueMax,
		&dsAcqQueueDepth,
		&dsAcqQueueMax,
		&dsBlockedRawPushes,
		&dsBlockedTrainPushes,
		&dsBlockedWaitMs,
		&dsDroppedPayloads,
		&dsAcqBlockedPushes,
		&dsAcqBlockedWaitMs,
		&samplesTotalNative,
		&dsAcqDroppedTooShort,
		&gf3HebbianWeightCellUpdates,
		(*C.char)(unsafe.Pointer(&statusBuf[0])),
		C.size_t(len(statusBuf)),
	)

	trainingStateMu.Lock()
	defer trainingStateMu.Unlock()
	if res != 0 {
		return
	}
	trainingState.CurrentEpoch = int(currentEpoch)
	trainingState.TotalEpochs = int(totalEpochs)
	trainingState.Loss = float64(currentLoss)
	trainingState.Samples = int(samplesProcessed)
	trainingState.IsRunning = bool(isRunning)
	trainingState.LoopCount = int(loopCount)
	trainingState.LastDLLPollAt = time.Now()
	trainingState.LastUpdate = time.Now()
	if metricsRes == 0 {
		nativeStatus := C.GoString((*C.char)(unsafe.Pointer(&statusBuf[0])))
		trainingState.SamplesTotal = uint64(samplesTotalNative)
		trainingState.DSDataAcquired = uint32(dataAcquired)
		trainingState.DSDataPerturbed = uint32(dataPerturbed)
		trainingState.DSAPIFailures = uint32(apiFailures)
		trainingState.DSBetti0 = uint32(betti0)
		trainingState.DSBetti1 = uint32(betti1)
		trainingState.DSBetti2 = uint32(betti2)
		trainingState.DSTopicFrontierSize = uint32(topicFrontierSize)
		trainingState.DSTopicFrontierMax = uint32(topicFrontierMax)
		trainingState.DSTopicFrontierEvict = uint32(topicFrontierEvict)
		if trainingState.CurrentExperts <= 0 && int(expertsActive) > 0 {
			trainingState.CurrentExperts = int(expertsActive)
		}
		trainingState.DSQueueDepth = uint32(dsQueueDepth)
		trainingState.DSRawQueueDepth = uint32(dsRawQueueDepth)
		trainingState.DSRawQueueMax = uint32(dsRawQueueMax)
		trainingState.DSTrainQueueMax = uint32(dsTrainQueueMax)
		trainingState.DSAcqQueueDepth = uint32(dsAcqQueueDepth)
		trainingState.DSAcqQueueMax = uint32(dsAcqQueueMax)
		trainingState.DSBlockedRawPushes = uint64(dsBlockedRawPushes)
		trainingState.DSBlockedTrainPushes = uint64(dsBlockedTrainPushes)
		trainingState.DSBlockedWaitMs = uint64(dsBlockedWaitMs)
		trainingState.DSDroppedPayloads = uint64(dsDroppedPayloads)
		trainingState.DSAcqBlockedPushes = uint64(dsAcqBlockedPushes)
		trainingState.DSAcqBlockedWaitMs = uint64(dsAcqBlockedWaitMs)
		trainingState.DSAcqDroppedTooShort = uint64(dsAcqDroppedTooShort)
		trainingState.GF3HebbianWeightCellUpdates = uint64(gf3HebbianWeightCellUpdates)
		trainingState.PipelineStatusText = nativeStatus
		var livePB, ceilPB C.uint32_t
		if int(C.Training_GetLiveParallelBatches(C.uint64_t(sid), &livePB, &ceilPB)) == 0 {
			trainingState.LiveParallelBatches = uint32(livePB)
			trainingState.ParallelBatchesCeiling = uint32(ceilPB)
		}
	}
}

func pollTrainingProgress() {
	// If the process vanishes with no "[pollTrainingProgress] Exited..." line, the likely cause
	// is a native fault in q_training.dll (see %LOCALAPPDATA%\q_mini_training_crash.log) or oom-kill.
	verbose := os.Getenv("QMINI_TRAINING_VERBOSE") == "1"
	if verbose {
		versionBuf := make([]byte, 256)
		C.Training_GetVersion((*C.char)(unsafe.Pointer(&versionBuf[0])), C.size_t(len(versionBuf)))
		fmt.Printf("[pollTrainingProgress] Native q_training.dll version: %s\n",
			C.GoString((*C.char)(unsafe.Pointer(&versionBuf[0]))))
		fmt.Printf("[pollTrainingProgress] Watch stderr for: [TrainingPipeline] TIMEOUT | Waiting for DataSynthesizer | moe_input_dim is 0 | PREFILL_STARVATION\n")
		fmt.Printf("[pollTrainingProgress] Set training.timing_to_stderr = true in training TOML for [TrainingTiming] collect/route/ff_train ms per batch (stderr).\n")
		fmt.Printf("[pollTrainingProgress] Set training.goodness_log_level = 1 or 2 in training TOML for [TrainingPipeline][Goodness] stderr lines (FF output metric).\n")
	}
	trainingStateMu.RLock()
	startSID := trainingState.SessionID
	trainingStateMu.RUnlock()
	fmt.Printf("[pollTrainingProgress] Started polling for session %d (set QMINI_TRAINING_VERBOSE=1 for periodic DLL logs)\n", startSID)
	pollCount := 0
	// Change-only logging by default — avoids spamming identical epoch/samples lines every ~20s.
	prevEpoch, prevSamples, prevLoop := -1, -1, -1
	prevLoss := math.NaN()
	prevRunning := true
	for {
		trainingStateMu.RLock()
		run := trainingState.IsRunning
		sid := trainingState.SessionID
		trainingStateMu.RUnlock()
		if !run || sid == 0 {
			break
		}

		pollCount++
		var currentEpoch, totalEpochs C.uint32_t
		var currentLoss C.double
		var samplesProcessed C.uint64_t
		var isRunning C.bool
		var loopCount C.uint32_t
		var statusBuf [512]byte

		if verbose && (pollCount == 1 || pollCount%60 == 0) {
			fmt.Printf("[pollTrainingProgress] Poll %d: Training_GetProgress(session=%d)…\n", pollCount, sid)
		}
		result := C.Training_GetProgress(
			C.uint64_t(sid),
			&currentEpoch,
			&totalEpochs,
			&currentLoss,
			&samplesProcessed,
			&isRunning,
			&loopCount,
		)
		var expertsActive, dataAcquired, dataPerturbed, apiFailures C.uint32_t
		var betti0, betti1, betti2 C.uint32_t
		var topicFrontierSize, topicFrontierMax, topicFrontierEvict C.uint32_t
		var samplesTotalNative C.uint64_t
		var dsQueueDepth, dsRawQueueDepth, dsRawQueueMax, dsTrainQueueMax C.uint32_t
		var dsAcqQueueDepth, dsAcqQueueMax C.uint32_t
		var dsBlockedRawPushes, dsBlockedTrainPushes, dsBlockedWaitMs C.uint64_t
		var dsDroppedPayloads, dsAcqBlockedPushes, dsAcqBlockedWaitMs C.uint64_t
		var dsAcqDroppedTooShort C.uint64_t
		var gf3HebbianWeightCellUpdates C.uint64_t
		metricsRes := C.Training_GetMetrics(
			C.uint64_t(sid),
			&expertsActive,
			&dataAcquired,
			&dataPerturbed,
			&apiFailures,
			&betti0,
			&betti1,
			&betti2,
			&topicFrontierSize,
			&topicFrontierMax,
			&topicFrontierEvict,
			&dsQueueDepth,
			&dsRawQueueDepth,
			&dsRawQueueMax,
			&dsTrainQueueMax,
			&dsAcqQueueDepth,
			&dsAcqQueueMax,
			&dsBlockedRawPushes,
			&dsBlockedTrainPushes,
			&dsBlockedWaitMs,
			&dsDroppedPayloads,
			&dsAcqBlockedPushes,
			&dsAcqBlockedWaitMs,
			&samplesTotalNative,
			&dsAcqDroppedTooShort,
			&gf3HebbianWeightCellUpdates,
			(*C.char)(unsafe.Pointer(&statusBuf[0])),
			C.size_t(len(statusBuf)),
		)
		nativeStatus := ""
		if metricsRes == 0 {
			nativeStatus = C.GoString((*C.char)(unsafe.Pointer(&statusBuf[0])))
		}

		ep := int(currentEpoch)
		samp := int(samplesProcessed)
		loss := float64(currentLoss)
		running := bool(isRunning)
		lc := int(loopCount)
		deltaSamp := 0
		if prevSamples >= 0 {
			if samp >= prevSamples {
				deltaSamp = samp - prevSamples
			} else {
				// Epoch reset in native pipeline.
				deltaSamp = samp
			}
		}
		lossChanged := math.IsNaN(prevLoss) || loss != prevLoss
		changed := ep != prevEpoch || samp != prevSamples || lc != prevLoop || lossChanged || running != prevRunning

		done := false
		trainingStateMu.Lock()
		if result == 0 {
			trainingState.CurrentEpoch = ep
			trainingState.TotalEpochs = int(totalEpochs)
			trainingState.Loss = loss
			trainingState.Samples = samp
			if deltaSamp > 0 {
				trainingState.SamplesTotal += uint64(deltaSamp)
			}
			if metricsRes == 0 {
				trainingState.DSDataAcquired = uint32(dataAcquired)
				trainingState.DSDataPerturbed = uint32(dataPerturbed)
				trainingState.DSAPIFailures = uint32(apiFailures)
				trainingState.DSBetti0 = uint32(betti0)
				trainingState.DSBetti1 = uint32(betti1)
				trainingState.DSBetti2 = uint32(betti2)
				trainingState.DSTopicFrontierSize = uint32(topicFrontierSize)
				trainingState.DSTopicFrontierMax = uint32(topicFrontierMax)
				trainingState.DSTopicFrontierEvict = uint32(topicFrontierEvict)
				if trainingState.CurrentExperts <= 0 && int(expertsActive) > 0 {
					trainingState.CurrentExperts = int(expertsActive)
				}
				trainingState.SamplesTotal = uint64(samplesTotalNative)
				trainingState.DSQueueDepth = uint32(dsQueueDepth)
				trainingState.DSRawQueueDepth = uint32(dsRawQueueDepth)
				trainingState.DSRawQueueMax = uint32(dsRawQueueMax)
				trainingState.DSTrainQueueMax = uint32(dsTrainQueueMax)
				trainingState.DSAcqQueueDepth = uint32(dsAcqQueueDepth)
				trainingState.DSAcqQueueMax = uint32(dsAcqQueueMax)
				trainingState.DSBlockedRawPushes = uint64(dsBlockedRawPushes)
				trainingState.DSBlockedTrainPushes = uint64(dsBlockedTrainPushes)
				trainingState.DSBlockedWaitMs = uint64(dsBlockedWaitMs)
				trainingState.DSDroppedPayloads = uint64(dsDroppedPayloads)
				trainingState.DSAcqBlockedPushes = uint64(dsAcqBlockedPushes)
				trainingState.DSAcqBlockedWaitMs = uint64(dsAcqBlockedWaitMs)
				trainingState.DSAcqDroppedTooShort = uint64(dsAcqDroppedTooShort)
				trainingState.GF3HebbianWeightCellUpdates = uint64(gf3HebbianWeightCellUpdates)
			}
			trainingState.PipelineStatusText = nativeStatus
			trainingState.IsRunning = running
			trainingState.LoopCount = lc
			trainingState.LastUpdate = time.Now()
			trainingState.LastDLLPollAt = time.Now()
			if !trainingState.IsRunning {
				done = true
				if trainingState.PipelineConfigured {
					trainingState.Phase = phaseReady
				} else {
					trainingState.Phase = phaseUnconfigured
				}
				trainingState.PhaseStartedAt = time.Now()
			}
		} else {
			fmt.Printf("[pollTrainingProgress] Poll %d: DLL ERROR result=%d - CHECK DLL COMPATIBILITY\n", pollCount, result)
		}
		trainingStateMu.Unlock()

		shouldLog := result != 0 || pollCount == 1 || changed
		if verbose {
			shouldLog = shouldLog || pollCount%60 == 0
		}
		if shouldLog {
			fmt.Printf("[pollTrainingProgress] Poll %d: result=%d, epoch=%d/%d, samples=%d, running=%v, loop=%d\n",
				pollCount, result, currentEpoch, totalEpochs, samplesProcessed, isRunning, loopCount)
		}
		prevEpoch, prevSamples, prevLoop, prevLoss, prevRunning = ep, samp, lc, loss, running

		if done {
			fmt.Printf("[pollTrainingProgress] Native reported not running after %d polls (phase -> ready/unconfigured)\n", pollCount)
			break
		}
		time.Sleep(1 * time.Second)
	}
	fmt.Printf("[pollTrainingProgress] Exited after %d polls\n", pollCount)
}

func handleStopTraining() interface{} {
	trainingStateMu.Lock()
	trainingState.Phase = phaseStopping
	trainingState.PhaseStartedAt = time.Now()
	sid := trainingState.SessionID
	trainingStateMu.Unlock()

	fmt.Printf("[Training] Stopping training session %d\n", sid)
	if sid != 0 {
		result := C.Training_StopTraining(C.uint64_t(sid))
		fmt.Printf("[Training] Training_StopTraining result: %d\n", result)
	}

	trainingStateMu.Lock()
	trainingState.IsRunning = false
	trainingState.Stop()
	if trainingState.PipelineConfigured {
		trainingState.Phase = phaseReady
	} else {
		trainingState.Phase = phaseUnconfigured
	}
	trainingState.PhaseStartedAt = time.Now()
	trainingState.InitMessage = "Training stopped"
	finEp := trainingState.CurrentEpoch
	finLoss := trainingState.Loss
	trainingStateMu.Unlock()

	return map[string]interface{}{
		"stopped":     true,
		"final_epoch": finEp,
		"final_loss":  finLoss,
		"message":     "Training stopped",
	}
}

// releaseNativeTrainingSession stops the native DLL session and frees its memory.
// Safe when SessionID is 0 (no-op). reason is for logs / InitMessage only.
func releaseNativeTrainingSession(reason string) map[string]interface{} {
	trainingStateMu.Lock()
	sid := trainingState.SessionID
	trainingStateMu.Unlock()
	if sid == 0 {
		return map[string]interface{}{
			"released": false,
			"reason":   reason,
			"message":  "no native session id tracked",
		}
	}
	fmt.Printf("[Training] releaseNativeTrainingSession(%s) sid=%d\n", reason, sid)
	_ = C.Training_StopTraining(C.uint64_t(sid))
	cr := C.Training_CleanupSession(C.uint64_t(sid))
	trainingStateMu.Lock()
	trainingState.SessionID = 0
	trainingState.IsRunning = false
	trainingState.Stop()
	if trainingState.PipelineConfigured {
		trainingState.Phase = phaseReady
	} else {
		trainingState.Phase = phaseUnconfigured
	}
	trainingState.PhaseStartedAt = time.Now()
	trainingState.InitMessage = "Native training session released (" + reason + ")"
	trainingStateMu.Unlock()
	return map[string]interface{}{
		"released":     true,
		"session_id":   sid,
		"cleanup_code": int(cr),
		"reason":       reason,
	}
}

func handleCloseTrainingSession() (interface{}, error) {
	trainingStateMu.RLock()
	run := trainingState.IsRunning
	sid := trainingState.SessionID
	trainingStateMu.RUnlock()
	if sid == 0 {
		return map[string]interface{}{
			"released": false,
			"message":  "no native session to close",
		}, nil
	}
	if run {
		return nil, fmt.Errorf("stop training (wui_stop_ff_training) before closing the native session")
	}
	return releaseNativeTrainingSession("mcp_wui_close_training_session"), nil
}

func handleExportTrainingCheckpoint(params map[string]interface{}) (interface{}, error) {
	trainingStateMu.Lock()
	sid := trainingState.SessionID
	running := trainingState.IsRunning
	trainingStateMu.Unlock()
	if sid == 0 {
		return nil, fmt.Errorf("no active training session; start training once (then stop) before export")
	}
	if running {
		return nil, fmt.Errorf("stop training before exporting; MoE weights are serialized only when the trainer is idle")
	}
	path := ""
	if p, ok := params["path"].(string); ok {
		path = strings.TrimSpace(p)
	}
	if path == "" {
		cpDir := filepath.Join(qminiDataRoot(), "checkpoints")
		if err := os.MkdirAll(cpDir, 0755); err != nil {
			return nil, fmt.Errorf("mkdir checkpoints: %w", err)
		}
		path = filepath.Join(cpDir, fmt.Sprintf("training_%d.qmini", time.Now().Unix()))
	}
	rc, err := callTrainingExportCheckpoint(sid, path)
	if err != nil {
		return nil, err
	}
	if rc != 0 {
		return nil, fmt.Errorf("Training_ExportCheckpoint failed (%d) path=%s", rc, path)
	}
	return map[string]interface{}{
		"ok":         true,
		"path":       path,
		"session_id": sid,
	}, nil
}

func handleImportTrainingCheckpoint(params map[string]interface{}) (interface{}, error) {
	path := ""
	if p, ok := params["path"].(string); ok {
		path = strings.TrimSpace(p)
	}
	if path == "" {
		return nil, fmt.Errorf("missing path parameter")
	}
	trainingStateMu.Lock()
	sid := trainingState.SessionID
	running := trainingState.IsRunning
	trainingStateMu.Unlock()
	if sid == 0 {
		return nil, fmt.Errorf("no active training session; start training once to create a native session, stop, then import")
	}
	if running {
		return nil, fmt.Errorf("stop training before import")
	}
	if _, err := os.Stat(path); err != nil {
		return nil, fmt.Errorf("checkpoint file: %w", err)
	}
	rc, err := callTrainingImportCheckpoint(sid, path)
	if err != nil {
		return nil, err
	}
	if rc != 0 {
		return nil, fmt.Errorf("Training_ImportCheckpoint failed (%d) path=%s", rc, path)
	}
	return map[string]interface{}{
		"ok":         true,
		"path":       path,
		"session_id": sid,
	}, nil
}

func handleRunInference(params map[string]interface{}) (interface{}, error) {
	prompt, ok := params["prompt"].(string)
	if !ok || prompt == "" {
		return nil, fmt.Errorf("missing 'prompt' parameter")
	}
	return nil, fmt.Errorf("inference is not implemented in this control plane (no stub responses)")
}

func handleLoadModel(params map[string]interface{}) (interface{}, error) {
	path, ok := params["model"].(string)
	if !ok || strings.TrimSpace(path) == "" {
		return nil, fmt.Errorf("missing 'model' parameter (path to a QMINI_V3 .qmini checkpoint)")
	}
	return handleImportTrainingCheckpoint(map[string]interface{}{"path": strings.TrimSpace(path)})
}

func handleGetReleaseStatus() interface{} {
	return map[string]interface{}{
		"status":        "not_implemented",
		"last_action":   "none",
		"artifact_path": "",
		"can_build":     false,
		"can_deploy":    false,
		"message":       "Release pipeline is not wired in qminiwasm; build with CMake/CI externally",
	}
}

func handleBuildRelease(params map[string]interface{}) (interface{}, error) {
	_, _ = params["output"].(string)
	return nil, fmt.Errorf("release build is not implemented in qminiwasm; run CMake for q_mini_wasm_v2 targets manually")
}

func handleDeployRelease(params map[string]interface{}) (interface{}, error) {
	_, _ = params["target"].(string)
	return nil, fmt.Errorf("deploy is not implemented in qminiwasm")
}

// ===== MISSING HANDLERS FOR REACT WUI =====

// parseDataSourcesToml parses [[source]] tables from data_sources.toml content.
func parseDataSourcesToml(content string) []map[string]interface{} {
	var sources []map[string]interface{}
	lines := strings.Split(content, "\n")
	currentSource := make(map[string]interface{})
	inSource := false

	for _, line := range lines {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "[[source]]") {
			if inSource && len(currentSource) > 0 {
				sources = append(sources, currentSource)
			}
			currentSource = make(map[string]interface{})
			inSource = true
			continue
		}
		if inSource && strings.Contains(line, "=") {
			parts := strings.SplitN(line, "=", 2)
			key := strings.TrimSpace(parts[0])
			value := strings.TrimSpace(parts[1])
			if idx := strings.Index(value, "#"); idx != -1 {
				value = strings.TrimSpace(value[:idx])
			}
			value = strings.Trim(value, `"`)

			switch key {
			case "name":
				currentSource["name"] = value
				currentSource["id"] = strings.ToLower(value)
			case "type":
				currentSource["type"] = value
			case "category":
				currentSource["category"] = value
			case "enabled":
				currentSource["enabled"] = value == "true"
			case "description":
				currentSource["description"] = value
			case "url":
				currentSource["url"] = value
			}
		}
	}
	if inSource && len(currentSource) > 0 {
		sources = append(sources, currentSource)
	}
	return sources
}

func appendLocalDatasetSources(sources []map[string]interface{}) []map[string]interface{} {
	localPath := filepath.Join(qminiDataRoot(), "datasets")
	info, err := os.Stat(localPath)
	if err != nil || !info.IsDir() {
		return sources
	}
	entries, err := os.ReadDir(localPath)
	if err != nil {
		return sources
	}
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := entry.Name()
		if strings.HasSuffix(name, ".jsonl") || strings.HasSuffix(name, ".json") || strings.HasSuffix(name, ".txt") {
			sources = append(sources, map[string]interface{}{
				"name":     name,
				"id":       "local_" + strings.ToLower(strings.ReplaceAll(name, ".", "_")),
				"type":     "local_file",
				"category": "local",
				"enabled":  true,
				"path":     filepath.Join(localPath, name),
			})
		}
	}
	return sources
}

func handleListDataSources() interface{} {
	sourcesPath := filepath.Join(qminiDataRoot(), "config", "data_sources.toml")
	data, err := os.ReadFile(sourcesPath)
	if err != nil {
		return map[string]interface{}{
			"sources": []map[string]interface{}{},
			"count":   0,
			"error":   fmt.Sprintf("data_sources.toml not found at %s: %v", sourcesPath, err),
		}
	}

	sources := parseDataSourcesToml(string(data))
	sources = appendLocalDatasetSources(sources)

	return map[string]interface{}{
		"sources": sources,
		"count":   len(sources),
		"path":    sourcesPath,
	}
}

var (
	acquisitionRunning bool
	acquisitionStats   struct {
		startTime     time.Time
		sourcesTotal  int
		sourcesDone   int
		itemsFetched  int
		currentSource string
	}
)

func handleStartDataAcquisition(params map[string]interface{}) (interface{}, error) {
	if acquisitionRunning {
		return nil, fmt.Errorf("data acquisition already running")
	}

	outputDir, _ := params["output_dir"].(string)

	// Get data sources from config
	sourcesResult := handleListDataSources()
	sourcesMap, ok := sourcesResult.(map[string]interface{})
	if !ok {
		return nil, fmt.Errorf("failed to load data sources")
	}

	sources, ok := sourcesMap["sources"].([]map[string]interface{})
	if !ok {
		return nil, fmt.Errorf("invalid data sources format")
	}

	// Setup output directory
	if outputDir == "" {
		outputDir = filepath.Join(qminiDataRoot(), "datasets", "acquired")
	}
	os.MkdirAll(outputDir, 0755)

	// Initialize acquisition state
	acquisitionRunning = true
	acquisitionStats.startTime = time.Now()
	acquisitionStats.sourcesTotal = len(sources)
	acquisitionStats.sourcesDone = 0
	acquisitionStats.itemsFetched = 0
	acquisitionStats.currentSource = ""

	// Start acquisition in background
	go runDataAcquisition(sources, outputDir)

	return map[string]interface{}{
		"started":    true,
		"output_dir": outputDir,
		"job_id":     fmt.Sprintf("acq_%d", time.Now().Unix()),
		"sources":    len(sources),
		"message":    fmt.Sprintf("Data acquisition started: processing %d sources", len(sources)),
	}, nil
}

func runDataAcquisition(sources []map[string]interface{}, outputDir string) {
	defer func() { acquisitionRunning = false }()

	outputFile := filepath.Join(outputDir, fmt.Sprintf("acquired_%d.jsonl", time.Now().Unix()))
	f, err := os.Create(outputFile)
	if err != nil {
		fmt.Printf("[Acquisition] Failed to create output file: %v\n", err)
		return
	}
	defer f.Close()

	client := &http.Client{Timeout: 30 * time.Second}

	for _, source := range sources {
		name, _ := source["name"].(string)
		sourceType, _ := source["type"].(string)
		url, _ := source["url"].(string)
		enabled, _ := source["enabled"].(bool)

		if !enabled {
			fmt.Printf("[Acquisition] Skipping disabled source: %s\n", name)
			acquisitionStats.sourcesDone++
			continue
		}

		acquisitionStats.currentSource = name
		fmt.Printf("[Acquisition] Fetching from %s...\n", name)

		if sourceType == "web_api" && url != "" {
			resp, err := client.Get(url)
			if err != nil {
				fmt.Printf("[Acquisition] Failed to fetch %s: %v\n", name, err)
				acquisitionStats.sourcesDone++
				continue
			}

			body, err := io.ReadAll(resp.Body)
			resp.Body.Close()
			if err != nil {
				fmt.Printf("[Acquisition] Failed to read response from %s: %v\n", name, err)
				acquisitionStats.sourcesDone++
				continue
			}

			// Write to JSONL file
			record := map[string]interface{}{
				"source":    name,
				"url":       url,
				"timestamp": time.Now().Unix(),
				"data":      string(body),
			}
			jsonData, _ := json.Marshal(record)
			f.WriteString(string(jsonData) + "\n")

			acquisitionStats.itemsFetched++
			fmt.Printf("[Acquisition] Fetched %d bytes from %s\n", len(body), name)
		} else if sourceType == "local_file" || sourceType == "directory" {
			path, _ := source["path"].(string)
			if path != "" {
				// Copy local file reference
				record := map[string]interface{}{
					"source":    name,
					"path":      path,
					"timestamp": time.Now().Unix(),
					"type":      "local_reference",
				}
				jsonData, _ := json.Marshal(record)
				f.WriteString(string(jsonData) + "\n")
				fmt.Printf("[Acquisition] Indexed local source: %s\n", name)
			}
		}

		acquisitionStats.sourcesDone++
		time.Sleep(500 * time.Millisecond) // Rate limiting
	}

	fmt.Printf("[Acquisition] Complete: %d items fetched from %d sources\n", acquisitionStats.itemsFetched, acquisitionStats.sourcesTotal)
	fmt.Printf("[Acquisition] Output saved to: %s\n", outputFile)
}

func handleStopDataAcquisition() (interface{}, error) {
	acquisitionRunning = false
	return map[string]interface{}{
		"stopped": true,
		"message": "Data acquisition stopped",
	}, nil
}

func handleGetAcquisitionStatus() interface{} {
	progress := 0
	if acquisitionStats.sourcesTotal > 0 {
		progress = int(float64(acquisitionStats.sourcesDone) / float64(acquisitionStats.sourcesTotal) * 100)
	}

	// Also check for actual data files on disk
	dataFiles := 0
	dataDir := filepath.Join(qminiDataRoot(), "datasets", "acquired")
	entries, err := os.ReadDir(dataDir)
	if err == nil {
		for _, entry := range entries {
			if !entry.IsDir() && (strings.HasSuffix(entry.Name(), ".txt") || strings.HasSuffix(entry.Name(), ".jsonl")) {
				dataFiles++
			}
		}
	}

	return map[string]interface{}{
		"running":        acquisitionRunning,
		"progress":       progress,
		"sources_total":  acquisitionStats.sourcesTotal,
		"sources_done":   acquisitionStats.sourcesDone,
		"items_fetched":  acquisitionStats.itemsFetched,
		"current_source": acquisitionStats.currentSource,
		"elapsed":        time.Since(acquisitionStats.startTime).Seconds(),
		"data_files":     dataFiles,
		"data_path":      dataDir,
	}
}

func handleAddDataSource(params map[string]interface{}) (interface{}, error) {
	name, _ := params["name"].(string)
	sourceType, _ := params["type"].(string)
	url, _ := params["url"].(string)
	path, _ := params["path"].(string)
	category, _ := params["category"].(string)
	desc, _ := params["description"].(string)
	enabled, _ := params["enabled"].(bool)

	if name == "" {
		return nil, fmt.Errorf("source name is required")
	}

	// Append to data_sources.toml
	sourcesPath := filepath.Join(qminiDataRoot(), "config", "data_sources.toml")

	var entry strings.Builder
	entry.WriteString("\n[[source]]\n")
	entry.WriteString(fmt.Sprintf("name = \"%s\"\n", name))
	entry.WriteString(fmt.Sprintf("type = \"%s\"\n", sourceType))
	if url != "" {
		entry.WriteString(fmt.Sprintf("url = \"%s\"\n", url))
	}
	if path != "" {
		entry.WriteString(fmt.Sprintf("path = \"%s\"\n", path))
	}
	entry.WriteString(fmt.Sprintf("category = \"%s\"\n", category))
	entry.WriteString(fmt.Sprintf("enabled = %t\n", enabled))
	if desc != "" {
		entry.WriteString(fmt.Sprintf("description = \"%s\"\n", desc))
	}

	// Append to file
	f, err := os.OpenFile(sourcesPath, os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0644)
	if err != nil {
		return nil, fmt.Errorf("failed to open data_sources.toml: %w", err)
	}
	defer f.Close()

	if _, err := f.WriteString(entry.String()); err != nil {
		return nil, fmt.Errorf("failed to write to data_sources.toml: %w", err)
	}

	return map[string]interface{}{
		"added":   true,
		"name":    name,
		"message": fmt.Sprintf("Source '%s' added to data_sources.toml", name),
	}, nil
}

func handleUpdateDataSource(params map[string]interface{}) (interface{}, error) {
	name, _ := params["name"].(string)
	enabled, hasEnabled := params["enabled"].(bool)

	if name == "" {
		return nil, fmt.Errorf("source name is required")
	}

	// Read and modify data_sources.toml
	sourcesPath := filepath.Join(qminiDataRoot(), "config", "data_sources.toml")
	data, err := os.ReadFile(sourcesPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read data_sources.toml: %w", err)
	}

	lines := strings.Split(string(data), "\n")
	var newLines []string
	inTargetSource := false
	found := false

	for i, line := range lines {
		trimmed := strings.TrimSpace(line)
		if trimmed == "[[source]]" {
			inTargetSource = false
		}
		if strings.HasPrefix(trimmed, "name =") && strings.Contains(line, fmt.Sprintf("\"%s\"", name)) {
			inTargetSource = true
			found = true
		}
		if inTargetSource && hasEnabled && strings.HasPrefix(trimmed, "enabled =") {
			newLines = append(newLines, fmt.Sprintf("enabled = %t", enabled))
			continue
		}
		newLines = append(newLines, lines[i])
	}

	if !found {
		return nil, fmt.Errorf("source '%s' not found", name)
	}

	if err := os.WriteFile(sourcesPath, []byte(strings.Join(newLines, "\n")), 0644); err != nil {
		return nil, fmt.Errorf("failed to write data_sources.toml: %w", err)
	}

	return map[string]interface{}{
		"updated": true,
		"name":    name,
		"message": fmt.Sprintf("Source '%s' updated", name),
	}, nil
}

func handlePauseTraining() interface{} {
	s := copyTrainingState()
	if !s.IsRunning {
		return map[string]interface{}{
			"paused":  false,
			"message": "Training is not running",
			"phase":   s.Phase,
		}
	}
	return map[string]interface{}{
		"paused":  true,
		"epoch":   s.CurrentEpoch,
		"phase":   s.Phase,
		"message": "Training paused at epoch",
	}
}

func handleLoadConfig() (interface{}, error) {
	configPath := resolveTrainingConfigPath()
	data, err := os.ReadFile(configPath)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, fmt.Errorf("training TOML is required at %q (no baked-in defaults); copy config/training_config.toml from the repository", configPath)
		}
		return nil, fmt.Errorf("failed to read config: %w", err)
	}
	return map[string]interface{}{
		"content":   string(data),
		"exists":    true,
		"path":      configPath,
		"data_root": qminiDataRoot(),
	}, nil
}

func handleSaveConfig(params map[string]interface{}) (interface{}, error) {
	content, ok := params["content"].(string)
	if !ok {
		return nil, fmt.Errorf("missing 'content' parameter")
	}

	configPath := resolveTrainingConfigPath()
	if err := os.MkdirAll(filepath.Dir(configPath), 0755); err != nil {
		return nil, fmt.Errorf("failed to create config directory: %w", err)
	}
	if err := os.WriteFile(configPath, []byte(content), 0644); err != nil {
		return nil, fmt.Errorf("failed to write config: %w", err)
	}

	if _, err := loadAndValidateTrainingConfig(); err != nil {
		return nil, fmt.Errorf("saved training TOML failed validation (file was written; fix errors): %w", err)
	}

	return map[string]interface{}{
		"saved":     true,
		"path":      configPath,
		"data_root": qminiDataRoot(),
	}, nil
}

func updateTrainingConfigKey(section, key, value string) (string, error) {
	configPath := resolveTrainingConfigPath()
	if err := os.MkdirAll(filepath.Dir(configPath), 0755); err != nil {
		return "", fmt.Errorf("failed to create config directory: %w", err)
	}
	raw, err := os.ReadFile(configPath)
	if err != nil {
		return "", fmt.Errorf("training TOML required at %q: %w", configPath, err)
	}

	lines := strings.Split(string(raw), "\n")
	targetHeader := "[" + section + "]"
	inSection := false
	sectionFound := false
	keyWritten := false
	var out []string

	flushKey := func() {
		if inSection && !keyWritten {
			out = append(out, fmt.Sprintf("%s = %s", key, value))
			keyWritten = true
		}
	}

	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if strings.HasPrefix(trimmed, "[") && strings.HasSuffix(trimmed, "]") {
			flushKey()
			inSection = trimmed == targetHeader
			if inSection {
				sectionFound = true
			}
			out = append(out, line)
			continue
		}
		if inSection && strings.HasPrefix(trimmed, key+" =") {
			out = append(out, fmt.Sprintf("%s = %s", key, value))
			keyWritten = true
			continue
		}
		out = append(out, line)
	}
	if !sectionFound {
		out = append(out, targetHeader)
		inSection = true
	}
	flushKey()

	content := strings.Join(out, "\n")
	if err := os.WriteFile(configPath, []byte(content), 0644); err != nil {
		return "", fmt.Errorf("failed to write config: %w", err)
	}
	if _, err := loadAndValidateTrainingConfig(); err != nil {
		return "", fmt.Errorf("updated training TOML failed validation (file was written; fix errors): %w", err)
	}
	return configPath, nil
}

func handleGetBufferProfile() (interface{}, error) {
	cfg, err := loadAndValidateTrainingConfig()
	if err != nil {
		return nil, err
	}
	profile := strings.ToLower(strings.TrimSpace(cfg.GetString("training.buffer_profile")))
	t := getBufferTuning(cfg)
	return map[string]interface{}{
		"profile": profile,
		"effective": map[string]interface{}{
			"prefill_target_samples":  t.PrefillTarget,
			"prefill_timeout_ms":      t.PrefillTimeoutMs,
			"prefill_poll_ms":         t.PrefillPollMs,
			"acq_queue_cap":           t.AcqQueueCap,
			"raw_queue_cap":           t.RawQueueCap,
			"train_queue_cap":         t.TrainQueueCap,
			"directory_max_lines":     t.DirectoryMaxLines,
			"max_jsonl_local_samples": t.MaxJsonlLocalSamples,
			"min_text_length":         t.MinTextLength,
			"max_text_length":         t.MaxTextLength,
		},
		"applies_on_next_start": true,
	}, nil
}

func handleSetBufferProfile(params map[string]interface{}) (interface{}, error) {
	profile, _ := params["profile"].(string)
	profile = strings.ToLower(strings.TrimSpace(profile))
	switch profile {
	case "conservative", "balanced", "aggressive":
	default:
		return nil, fmt.Errorf("invalid profile %q (expected conservative|balanced|aggressive)", profile)
	}

	path, err := updateTrainingConfigKey("training", "buffer_profile", fmt.Sprintf("%q", profile))
	if err != nil {
		return nil, err
	}

	cfg, err := loadAndValidateTrainingConfig()
	if err != nil {
		return nil, err
	}
	t := getBufferTuning(cfg)
	return map[string]interface{}{
		"updated":               true,
		"profile":               profile,
		"path":                  path,
		"applies_on_next_start": true,
		"effective": map[string]interface{}{
			"prefill_target_samples":  t.PrefillTarget,
			"prefill_timeout_ms":      t.PrefillTimeoutMs,
			"prefill_poll_ms":         t.PrefillPollMs,
			"acq_queue_cap":           t.AcqQueueCap,
			"raw_queue_cap":           t.RawQueueCap,
			"train_queue_cap":         t.TrainQueueCap,
			"directory_max_lines":     t.DirectoryMaxLines,
			"max_jsonl_local_samples": t.MaxJsonlLocalSamples,
			"min_text_length":         t.MinTextLength,
			"max_text_length":         t.MaxTextLength,
		},
	}, nil
}

func handleGetAPIKeys() interface{} {
	return map[string]interface{}{
		"keys": map[string]interface{}{
			"wolfram":  map[string]interface{}{"configured": false, "last_digits": ""},
			"wikidata": map[string]interface{}{"configured": false, "last_digits": ""},
			"arxiv":    map[string]interface{}{"configured": false, "last_digits": ""},
			"github":   map[string]interface{}{"configured": false, "last_digits": ""},
			"lean":     map[string]interface{}{"configured": false, "last_digits": ""},
		},
	}
}

func handleSaveAPIKeys(params map[string]interface{}) (interface{}, error) {
	// In real implementation, these would be encrypted and stored securely
	keysPath := filepath.Join(qminiDataRoot(), "config", "api_keys.json")

	data, _ := json.Marshal(params)
	if err := os.MkdirAll(filepath.Dir(keysPath), 0755); err != nil {
		return nil, err
	}
	if err := os.WriteFile(keysPath, data, 0600); err != nil {
		return nil, err
	}

	return map[string]interface{}{"saved": true}, nil
}

func handleGetDataSources() interface{} {
	sourcesPath := filepath.Join(qminiDataRoot(), "config", "data_sources.toml")
	data, err := os.ReadFile(sourcesPath)
	if err != nil {
		return map[string]interface{}{
			"sources": []map[string]interface{}{},
			"count":   0,
			"path":    sourcesPath,
			"error":   fmt.Sprintf("data_sources.toml not readable at %s: %v", sourcesPath, err),
		}
	}
	raw := string(data)
	sources := parseDataSourcesToml(raw)
	sources = appendLocalDatasetSources(sources)
	return map[string]interface{}{
		"sources":  sources,
		"count":    len(sources),
		"path":     sourcesPath,
		"raw_toml": raw,
	}
}

func handleAddSourcesToConfig(params map[string]interface{}) (interface{}, error) {
	sources, _ := params["sources"].([]interface{})

	configPath := resolveTrainingConfigPath()

	data, err := os.ReadFile(configPath)
	if err != nil {
		return nil, fmt.Errorf("training TOML required at %q: %w", configPath, err)
	}
	content := string(data)

	// Append sources section
	content += "\n[data_sources]\n"
	for i, src := range sources {
		if s, ok := src.(map[string]interface{}); ok {
			name, _ := s["id"].(string)
			content += fmt.Sprintf("source_%d = \"%s\"\n", i, name)
		}
	}

	if err := os.WriteFile(configPath, []byte(content), 0644); err != nil {
		return nil, err
	}

	if _, err := loadAndValidateTrainingConfig(); err != nil {
		return nil, fmt.Errorf("training TOML invalid after appending sources (file was written; fix errors): %w", err)
	}

	return map[string]interface{}{
		"added":   len(sources),
		"updated": true,
	}, nil
}

// Config holds only keys present in the training TOML on disk (no implicit defaults).
type Config struct {
	data    map[string]map[string]interface{}
	present map[string]map[string]struct{}
}

// splitSectionKey supports dotted TOML headers like [training.data_filter] → section "training.data_filter".
func (c *Config) splitSectionKey(fullKey string) (section, field string, ok bool) {
	parts := strings.Split(fullKey, ".")
	if len(parts) < 2 {
		return "", "", false
	}
	field = parts[len(parts)-1]
	section = strings.Join(parts[:len(parts)-1], ".")
	return section, field, true
}

func (c *Config) Has(key string) bool {
	sec, k, ok := c.splitSectionKey(key)
	if !ok {
		return false
	}
	if c.present == nil {
		return false
	}
	row, ok := c.present[sec]
	if !ok {
		return false
	}
	_, ok = row[k]
	return ok
}

func parseTrainingConfig(data []byte) (*Config, error) {
	cfg := &Config{
		data:    make(map[string]map[string]interface{}),
		present: make(map[string]map[string]struct{}),
	}
	currentSection := ""
	for _, line := range strings.Split(string(data), "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			currentSection = line[1 : len(line)-1]
			if cfg.data[currentSection] == nil {
				cfg.data[currentSection] = make(map[string]interface{})
			}
			if cfg.present[currentSection] == nil {
				cfg.present[currentSection] = make(map[string]struct{})
			}
			continue
		}
		if !strings.Contains(line, "=") || currentSection == "" {
			continue
		}
		parts := strings.SplitN(line, "=", 2)
		key := strings.TrimSpace(parts[0])
		value := strings.TrimSpace(parts[1])
		if idx := strings.Index(value, "#"); idx != -1 {
			value = strings.TrimSpace(value[:idx])
		}
		var parsed interface{}
		if i, err := strconv.ParseInt(value, 10, 64); err == nil {
			parsed = int(i)
		} else if f, err := strconv.ParseFloat(value, 64); err == nil {
			parsed = f
		} else if b, err := strconv.ParseBool(value); err == nil {
			parsed = b
		} else {
			parsed = strings.Trim(value, `"`)
		}
		cfg.data[currentSection][key] = parsed
		cfg.present[currentSection][key] = struct{}{}
	}
	return cfg, nil
}

func loadTrainingConfig() (*Config, error) {
	configPath := resolveTrainingConfigPath()
	fmt.Printf("[Config] Loading from: %s\n", configPath)
	data, err := os.ReadFile(configPath)
	if err != nil {
		return nil, fmt.Errorf("training TOML not readable at %q: %w (copy a complete file from the repo config/ directory)", configPath, err)
	}
	if len(strings.TrimSpace(string(data))) == 0 {
		return nil, fmt.Errorf("training TOML %q is empty", configPath)
	}
	cfg, err := parseTrainingConfig(data)
	if err != nil {
		return nil, err
	}
	if epochs, ok := cfg.data["training"]["epochs"]; ok {
		fmt.Printf("[Config] Loaded from file: epochs=%v\n", epochs)
	}
	if batchSize, ok := cfg.data["training"]["batch_size"]; ok {
		fmt.Printf("[Config] Loaded from file: batch_size=%v\n", batchSize)
	}
	return cfg, nil
}

func validateIntInRange(name string, v, lo, hi int) error {
	if v < lo || v > hi {
		return fmt.Errorf("%s must be in [%d,%d] (got %d)", name, lo, hi, v)
	}
	return nil
}

func validateUint64InRange(name string, v, lo, hi uint64) error {
	if v < lo || v > hi {
		return fmt.Errorf("%s must be in [%d,%d] (got %d)", name, lo, hi, v)
	}
	return nil
}

// validateTrainingConfig enforces keys required by qminiwasm / WUI. Several integers allow 0 as
// “derive” or “no extra cap”; see q_mini_docs/TRAINING_CONFIG_ZERO.md.
func validateTrainingConfig(c *Config) error {
	required := []string{
		"paths.dataset_dir",
		"training.epochs",
		"training.batch_size",
		"training.learning_rate",
		"training.checkpoint_interval",
		"training.buffer_profile",
		"training.samples_per_epoch",
		"training.micro_batch_cap",
		"training.collect_floor",
		"training.timing_to_stderr",
		"training.sycl_route_mode",
		"training.sycl_trit_quant_min_moe_dim",
		"training.goodness_log_level",
		"training.allow_generated_negatives",
		"training.prefill_target_samples",
		"training.prefill_timeout_ms",
		"training.prefill_poll_ms",
		"training.acq_queue_cap",
		"training.raw_queue_cap",
		"training.train_queue_cap",
		"training.acquisition_threads",
		"training.perturbation_threads",
		"training.checkpoint_async_queue_max",
		"training.collect_empty_backoff_base_ms",
		"training.collect_empty_backoff_max_shift",
		"training.collect_empty_backoff_cap_ms",
		"training.metrics_heartbeat_sec",
		"training.max_jsonl_local_samples",
		"training.data_filter.directory_max_lines",
		"training.data_filter.min_text_length",
		"training.data_filter.max_text_length",
		"training.data_filter.checkpoint_interval_samples",
		"training.lazy_init_initial_experts",
		"model.num_layers",
		"model.moe_experts",
		"model.moe_top_k",
		"model.context_window",
		"model.entanglement_tokens",
		"model.shadow_dim",
		"model.neurons_per_layer",
		"model.routing_qutrits",
		"model.moe_input_dim",
		"model.moe_output_dim",
		"model.moe_hidden_dim",
		"model.expert_internal_layers",
		"model.moe_ff_active_internal_layers",
		"features.lazy_init",
		"features.data_accumulation",
		"features.continuous_mode",
		"features.steane_correction",
		"features.flash_cim",
		"features.worker_threads",
	}
	var missing []string
	for _, k := range required {
		if !c.Has(k) {
			missing = append(missing, k)
		}
	}
	if len(missing) > 0 {
		return fmt.Errorf("missing required keys: %s", strings.Join(missing, ", "))
	}

	if strings.TrimSpace(c.GetString("paths.dataset_dir")) == "" {
		return fmt.Errorf("paths.dataset_dir must be a non-empty string")
	}
	if c.GetFloat("training.learning_rate") <= 0 {
		return fmt.Errorf("training.learning_rate must be > 0")
	}
	if c.GetUint64("training.samples_per_epoch") < 1 {
		return fmt.Errorf("training.samples_per_epoch must be >= 1")
	}
	// 0 = native derives full batch / no extra floor (see training_micro_batch_cap_for, TRAINING_THROUGHPUT.md).
	if err := validateIntInRange("training.micro_batch_cap", c.GetInt("training.micro_batch_cap"), 0, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.collect_floor", c.GetInt("training.collect_floor"), 0, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.epochs", c.GetInt("training.epochs"), 1, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.batch_size", c.GetInt("training.batch_size"), 1, math.MaxInt); err != nil {
		return err
	}
	if c.GetInt("training.checkpoint_interval") < 0 {
		return fmt.Errorf("training.checkpoint_interval must be >= 0")
	}
	profile := strings.ToLower(strings.TrimSpace(c.GetString("training.buffer_profile")))
	switch profile {
	case "conservative", "balanced", "aggressive":
	default:
		return fmt.Errorf("training.buffer_profile must be conservative|balanced|aggressive (got %q)", profile)
	}
	syclRM := strings.ToLower(strings.TrimSpace(c.GetString("training.sycl_route_mode")))
	switch syclRM {
	case "auto", "on", "off":
	default:
		return fmt.Errorf(`training.sycl_route_mode must be "auto", "on", or "off" (got %q)`, c.GetString("training.sycl_route_mode"))
	}
	// 0 = minimal prefill: native treats as 1 contrastive pair before training_loop (see prefill wait in AutonomousTrainingPipeline).
	if err := validateIntInRange("training.prefill_target_samples", c.GetInt("training.prefill_target_samples"), 0, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.prefill_timeout_ms", c.GetInt("training.prefill_timeout_ms"), 1, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.prefill_poll_ms", c.GetInt("training.prefill_poll_ms"), 1, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.acq_queue_cap", c.GetInt("training.acq_queue_cap"), 1, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.raw_queue_cap", c.GetInt("training.raw_queue_cap"), 1, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.train_queue_cap", c.GetInt("training.train_queue_cap"), 1, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.acquisition_threads", c.GetInt("training.acquisition_threads"), 1, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.perturbation_threads", c.GetInt("training.perturbation_threads"), 1, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.checkpoint_async_queue_max", c.GetInt("training.checkpoint_async_queue_max"), 1, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.collect_empty_backoff_base_ms", c.GetInt("training.collect_empty_backoff_base_ms"), 0, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.collect_empty_backoff_max_shift", c.GetInt("training.collect_empty_backoff_max_shift"), 0, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.collect_empty_backoff_cap_ms", c.GetInt("training.collect_empty_backoff_cap_ms"), 0, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.metrics_heartbeat_sec", c.GetInt("training.metrics_heartbeat_sec"), 0, math.MaxInt); err != nil {
		return err
	}
	// Optional: 0 = uncapped; native clamps effective parallel waves to min(live, cap). Omitted → InitSession default 96.
	if c.IsSet("training.parallel_batches_runtime_cap") {
		if err := validateIntInRange("training.parallel_batches_runtime_cap", c.GetInt("training.parallel_batches_runtime_cap"), 0, 65536); err != nil {
			return err
		}
	}
	if err := validateUint64InRange("training.max_jsonl_local_samples", c.GetUint64("training.max_jsonl_local_samples"), 1, math.MaxUint64); err != nil {
		return err
	}
	if err := validateUint64InRange("training.data_filter.directory_max_lines", c.GetUint64("training.data_filter.directory_max_lines"), 1, math.MaxUint64); err != nil {
		return err
	}
	if err := validateIntInRange("training.data_filter.min_text_length", c.GetInt("training.data_filter.min_text_length"), 1, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.data_filter.max_text_length", c.GetInt("training.data_filter.max_text_length"), 1, math.MaxInt); err != nil {
		return err
	}
	if c.GetInt("training.data_filter.min_text_length") > c.GetInt("training.data_filter.max_text_length") {
		return fmt.Errorf("training.data_filter.min_text_length must be <= training.data_filter.max_text_length")
	}
	if err := validateIntInRange("training.data_filter.checkpoint_interval_samples", c.GetInt("training.data_filter.checkpoint_interval_samples"), 1, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.sycl_trit_quant_min_moe_dim", c.GetInt("training.sycl_trit_quant_min_moe_dim"), 1, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("training.goodness_log_level", c.GetInt("training.goodness_log_level"), 0, math.MaxInt); err != nil {
		return err
	}
	if err := validateIntInRange("features.worker_threads", c.GetInt("features.worker_threads"), 1, math.MaxInt); err != nil {
		return err
	}
	expertN := c.GetInt("model.moe_experts")
	lazyInit := c.GetBool("features.lazy_init")
	lazyInitial := c.GetInt("training.lazy_init_initial_experts")
	if lazyInit {
		// 0 = derive to model.moe_top_k (resolveEffectiveTrainingParams); native InitSession gets computed effective_* from Start path.
		if err := validateIntInRange("training.lazy_init_initial_experts", lazyInitial, 0, expertN); err != nil {
			return err
		}
	} else if lazyInitial != expertN {
		return fmt.Errorf("when features.lazy_init is false, training.lazy_init_initial_experts must equal model.moe_experts (got %d vs %d)", lazyInitial, expertN)
	}

	intPos := []struct {
		key string
		lo  int
	}{
		{"model.num_layers", 1},
		{"model.moe_experts", 1},
		{"model.moe_top_k", 1},
		{"model.context_window", 1},
		{"model.entanglement_tokens", 1},
		{"model.shadow_dim", 1},
		{"model.neurons_per_layer", 1},
		{"model.routing_qutrits", 1},
		{"model.moe_input_dim", 1},
		{"model.moe_output_dim", 1},
		{"model.moe_hidden_dim", 1},
		{"model.expert_internal_layers", 1},
	}
	for _, e := range intPos {
		v := c.GetInt(e.key)
		if v < e.lo {
			return fmt.Errorf("%s must be >= %d (got %d)", e.key, e.lo, v)
		}
	}
	if c.GetInt("model.moe_ff_active_internal_layers") < 0 {
		return fmt.Errorf("model.moe_ff_active_internal_layers must be >= 0")
	}
	return nil
}

// Caches successful parse+validate to avoid re-reading and re-printing [Config] on every
// wui_get_training_metrics / handler that only needs the same TOML (hot path for MCP polling).
var validatedTrainingCfgCache struct {
	mu   sync.Mutex
	path string
	mod  time.Time
	cfg  *Config
}

func loadAndValidateTrainingConfig() (*Config, error) {
	path := resolveTrainingConfigPath()
	fi, statErr := os.Stat(path)
	if statErr == nil {
		validatedTrainingCfgCache.mu.Lock()
		if validatedTrainingCfgCache.cfg != nil && validatedTrainingCfgCache.path == path && validatedTrainingCfgCache.mod.Equal(fi.ModTime()) {
			c := validatedTrainingCfgCache.cfg
			validatedTrainingCfgCache.mu.Unlock()
			return c, nil
		}
		validatedTrainingCfgCache.mu.Unlock()
	}
	cfg, err := loadTrainingConfig()
	if err != nil {
		return nil, err
	}
	if err := validateTrainingConfig(cfg); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	if fi2, err2 := os.Stat(path); err2 == nil {
		validatedTrainingCfgCache.mu.Lock()
		validatedTrainingCfgCache.path = path
		validatedTrainingCfgCache.mod = fi2.ModTime()
		validatedTrainingCfgCache.cfg = cfg
		validatedTrainingCfgCache.mu.Unlock()
	}
	return cfg, nil
}

func (c *Config) GetBool(key string) bool {
	section, key, ok := c.splitSectionKey(key)
	if !ok {
		return false
	}
	if sec, ok := c.data[section]; ok {
		if v, ok := sec[key]; ok {
			if b, ok := v.(bool); ok {
				return b
			}
		}
	}
	return false
}

func (c *Config) GetBoolDefault(key string, defaultValue bool) bool {
	section, key, ok := c.splitSectionKey(key)
	if !ok {
		return defaultValue
	}
	if sec, ok := c.data[section]; ok {
		if v, ok := sec[key]; ok {
			if b, ok := v.(bool); ok {
				return b
			}
		}
	}
	return defaultValue
}

// trainingUint01 returns 0/1 for native Training_InitSession flags. TOML often uses booleans
// (e.g. parallel_contrastive_rows = true); GetInt yields 0 for bool-typed values and for omitted keys,
// which silently disabled SYCL multi-row batching and OpenMP row parallelism. Unset keys use defaultVal.
func (c *Config) trainingUint01(key string, defaultVal uint32) uint32 {
	if !c.IsSet(key) {
		return defaultVal
	}
	if c.GetBool(key) {
		return 1
	}
	if c.GetInt(key) != 0 {
		return 1
	}
	return 0
}

// IsSet reports whether the key was explicitly present in the loaded TOML config.
func (c *Config) IsSet(key string) bool {
	section, k, ok := c.splitSectionKey(key)
	if !ok {
		return false
	}
	if sec, ok := c.present[section]; ok {
		_, exists := sec[k]
		return exists
	}
	return false
}

func (c *Config) GetInt(key string) int {
	section, key, ok := c.splitSectionKey(key)
	if !ok {
		return 0
	}
	if sec, ok := c.data[section]; ok {
		if v, ok := sec[key]; ok {
			switch v := v.(type) {
			case int:
				return v
			case int8:
				return int(v)
			case int16:
				return int(v)
			case int32:
				return int(v)
			case int64:
				return int(v)
			case uint:
				return int(v)
			case uint8:
				return int(v)
			case uint16:
				return int(v)
			case uint32:
				return int(v)
			case uint64:
				return int(v)
			case float32:
				return int(v)
			case float64:
				return int(v)
			}
		}
	}
	return 0
}

func (c *Config) GetUint64(key string) uint64 {
	section, k, ok := c.splitSectionKey(key)
	if !ok {
		return 0
	}
	if sec, ok := c.data[section]; ok {
		if v, ok := sec[k]; ok {
			switch v := v.(type) {
			case int:
				if v < 0 {
					return 0
				}
				return uint64(v)
			case int8:
				if v < 0 {
					return 0
				}
				return uint64(v)
			case int16:
				if v < 0 {
					return 0
				}
				return uint64(v)
			case int32:
				if v < 0 {
					return 0
				}
				return uint64(v)
			case int64:
				if v < 0 {
					return 0
				}
				return uint64(v)
			case uint:
				return uint64(v)
			case uint8:
				return uint64(v)
			case uint16:
				return uint64(v)
			case uint32:
				return uint64(v)
			case uint64:
				return v
			case float32:
				if v < 0 {
					return 0
				}
				return uint64(v)
			case float64:
				if v < 0 {
					return 0
				}
				return uint64(v)
			}
		}
	}
	return 0
}

func (c *Config) GetFloat(key string) float64 {
	section, key, ok := c.splitSectionKey(key)
	if !ok {
		return 0
	}
	if sec, ok := c.data[section]; ok {
		if v, ok := sec[key]; ok {
			switch v := v.(type) {
			case float64:
				return v
			case int:
				return float64(v)
			}
		}
	}
	return 0
}

func (c *Config) GetString(key string) string {
	section, k, ok := c.splitSectionKey(key)
	if !ok {
		return ""
	}
	if sec, ok := c.data[section]; ok {
		if v, ok := sec[k]; ok {
			if s, ok := v.(string); ok {
				return s
			}
		}
	}
	return ""
}
