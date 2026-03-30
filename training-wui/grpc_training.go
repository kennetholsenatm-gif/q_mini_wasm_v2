package main

// Regenerate stubs from repo root:
//
//	go generate ./...
//
// (from training-wui: protoc -I ../proto ../proto/training_engine.proto --go_out=./trainingrpc --go_opt=paths=source_relative --go-grpc_out=./trainingrpc --go-grpc_opt=paths=source_relative)
//
//go:generate protoc -I ../proto ../proto/training_engine.proto --go_out=./trainingrpc --go_opt=paths=source_relative --go-grpc_out=./trainingrpc --go-grpc_opt=paths=source_relative

import (
	"context"
	"fmt"
	"math"
	"net"
	"os"
	"strings"
	"sync"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/grpcrunner"
	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingrpc"
)

var (
	trainingGRPCMu   sync.Mutex
	trainingGRPCConn *grpc.ClientConn
)

func trainingGRPCEnsureConn(ctx context.Context) (*grpc.ClientConn, error) {
	trainingGRPCMu.Lock()
	defer trainingGRPCMu.Unlock()
	if trainingGRPCConn != nil {
		return trainingGRPCConn, nil
	}
	addr := trainingGRPCAddressResolved()
	c, err := grpc.NewClient(addr, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		return nil, err
	}
	trainingGRPCConn = c
	return c, nil
}

func useNativeTrainingEngineGRPC(opts runStartOpts) bool {
	if opts.RunTarget == "runpod" && opts.RunpodTrainOnPod {
		return false
	}
	if opts.CascadeRequiresNativeGRPC {
		return true
	}
	useGRPC, _ := trainingEnginePickLocal()
	return useGRPC
}

func trainingGRPCReachable(timeout time.Duration) bool {
	addr := trainingGRPCAddressResolved()
	d := net.Dialer{Timeout: timeout}
	c, err := d.Dial("tcp", addr)
	if err != nil {
		return false
	}
	_ = c.Close()
	return true
}

// grpcEstimatedTotalEpochs sets a WUI progress denominator for gRPC runs. Matches native cascade
// budgeting when cascade_loop.enabled (teacher + heal, scaled by max_curriculum_cycles); else cfg.epochs.
// When training_phases is set, cfg.epochs is already sum(phase.epochs) (Unified Training Matrix); do not apply cascade_loop expansion.
func grpcEstimatedTotalEpochs(cfg *trainingrpc.TrainingConfig) int {
	if cfg == nil {
		return 1
	}
	e := int(cfg.GetEpochs())
	if e < 1 {
		e = 1
	}
	if len(cfg.GetTrainingPhases()) > 0 {
		return e
	}
	cl := cfg.GetCascadeLoop()
	if cl == nil || !cl.GetEnabled() {
		return e
	}
	frac := cl.GetTeacherEpochFraction()
	if frac < 0.05 {
		frac = 0.05
	}
	teacher := int(math.Max(1, math.Ceil(float64(e)*frac)))
	maxHeal := cl.GetMaxHealRounds()
	if maxHeal < 1 {
		maxHeal = 1
	}
	hpr := cl.GetHealEpochsPerRound()
	if hpr < 1 {
		hpr = 1
	}
	healTotal := e - teacher
	if healTotal < 1 || e <= teacher {
		healTotal = int(hpr) * int(maxHeal)
		if healTotal < 1 {
			healTotal = 1
		}
	}
	mc := cl.GetMaxCurriculumCycles()
	if mc < 1 {
		mc = 1
	}
	return teacher + healTotal*int(mc)
}

func appendNativeGRPCLogLine(rec *runRecord, line string) {
	if rec == nil || rec.cmd != nil {
		return
	}
	rec.logMu.Lock()
	rec.logBuf.WriteString(line)
	rec.logMu.Unlock()
}

func (m *manager) broadcastTelemetryProto(runID string, ev *trainingrpc.TelemetryEvent) {
	ts := time.Now().UTC().Format(time.RFC3339)
	epoch := float64(ev.GetEpoch())
	trainLoss := ev.GetTrainLoss()
	valLoss := ev.GetValLoss()
	msg := ev.GetMessage()
	eventType := strings.ToLower(ev.GetEventType())

	wsHub.broadcast(runID, buildGRPCMetricWebSocketPayload(runID, ev, ts))

	if eventType == "epoch_throughput" && ev.GetEpochWallS() > 0 {
		m.mu.Lock()
		tot := 0
		if r := m.byID[runID]; r != nil {
			tot = r.TotalEpochs
		}
		m.mu.Unlock()
		batches := int(ev.GetEpochBatchCount())
		samples := int(ev.GetEpochSampleCount())
		bs := 0
		if batches > 0 && samples > 0 {
			bs = samples / batches
		}
		ttPayload := map[string]any{
			"type":               "train_throughput",
			"run_id":             runID,
			"ts":                 ts,
			"epoch":              int(ev.GetEpoch()) + 1, // 1-based for WUI (proto epoch is 0-based)
			"epochs_total":       tot,
			"wall_s":             ev.GetEpochWallS(),
			"batches":            batches,
			"samples":            samples,
			"batch_size":         bs,
			"samples_per_s":      ev.GetEpochMeanSamplesPerS(),
			"batches_per_s":      float64(batches) / ev.GetEpochWallS(),
			"dataloader_workers": 0,
			"cascade_s":          0.0,
			"telemetry_source":   telemetrySourceGRPCCPP,
		}
		if ev.GetHostRssMib() > 0 {
			ttPayload["host_rss_mib"] = ev.GetHostRssMib()
		}
		wsHub.broadcast(runID, ttPayload)
	}
	if eventType == "enclave_summary" {
		tier := strings.TrimSpace(ev.GetTaxonomyTier())
		if tier == "" {
			tier = strings.TrimSpace(ev.GetEnclaveState())
		}
		wsHub.broadcast(runID, map[string]any{
			"type":               "enclave_telemetry",
			"run_id":             runID,
			"ts":                 ts,
			"estimated_tpem_mb":  ev.GetEstimatedTpemMib(),
			"tier_cap_mb":        ev.GetTierCapMib(),
			"enclave_tier":       tier,
			"use_memory64":       0,
			"line":               msg,
			"telemetry_source":   telemetrySourceGRPCCPP,
		})
	}

	m.mu.Lock()
	rec := m.byID[runID]
	if rec != nil {
		eph := int(epoch)
		if eventType == "epoch_throughput" {
			alt := int(ev.GetEpoch()) + 1
			if alt > eph {
				eph = alt
			}
		}
		if eph > rec.LastEpoch {
			rec.LastEpoch = eph
		}
		rec.LastMeanLoss = trainLoss
		rec.LastMeanMSE = valLoss
		rec.LastMeanReturn = 0.0
	}
	m.mu.Unlock()

	if rec != nil && rec.cmd == nil {
		safeMsg := strings.ReplaceAll(strings.TrimSpace(msg), "\n", " ")
		if len(safeMsg) > 160 {
			safeMsg = safeMsg[:160] + "..."
		}
		stage := strings.TrimSpace(ev.GetStage())
		line := fmt.Sprintf(
			"[native:telemetry] stage=%s event=%s epoch=%d train=%.6g val=%.6g",
			stage, eventType, ev.GetEpoch(), trainLoss, valLoss,
		)
		if safeMsg != "" {
			line += fmt.Sprintf(" msg=%q", safeMsg)
		}
		line += "\n"
		appendNativeGRPCLogLine(rec, line)
	}

	if eventType == "error" || strings.Contains(strings.ToLower(msg), "fallback") {
		wsHub.broadcast(runID, map[string]any{
			"type":     "alert",
			"run_id":   runID,
			"ts":       time.Now().UTC().Format(time.RFC3339),
			"severity": "critical",
			"code":     "training_engine_grpc_alert",
			"message":  msg,
		})
	}
	if strings.Contains(msg, "qmw_backend_status") {
		wsHub.broadcast(runID, map[string]any{
			"type":             "backend_status",
			"run_id":           runID,
			"ts":               time.Now().UTC().Format(time.RFC3339),
			"line":             msg,
			"telemetry_source": telemetrySourceGRPCCPP,
		})
	}

	// Synthetic log line so web/index.html log poller can pick up enclave-style telemetry without JS changes.
	if (strings.TrimSpace(ev.GetEnclaveState()) != "" || strings.TrimSpace(ev.GetAttestationState()) != "") &&
		!strings.EqualFold(strings.TrimSpace(ev.GetEventType()), "enclave_summary") {
		line := fmt.Sprintf(
			"qmw_enclave_telemetry estimated_tpem_mb=0.000 tier_cap_mb=-1.0 enclave_tier=grpc use_memory64=0 stage=%s enclave=%s attestation=%s\n",
			strings.TrimSpace(ev.GetStage()),
			strings.TrimSpace(ev.GetEnclaveState()),
			strings.TrimSpace(ev.GetAttestationState()),
		)
		m.mu.Lock()
		rec := m.byID[runID]
		m.mu.Unlock()
		if rec != nil && rec.cmd == nil {
			rec.logMu.Lock()
			rec.logBuf.WriteString(line)
			rec.logMu.Unlock()
		}
	}
}

func (m *manager) grpcCallStopTraining(ctx context.Context, runID string) error {
	conn, err := trainingGRPCEnsureConn(ctx)
	if err != nil {
		return err
	}
	cli := trainingrpc.NewTrainingEngineServiceClient(conn)
	_, err = cli.StopTraining(ctx, &trainingrpc.StopTrainingRequest{RunId: runID})
	return err
}

// runNativeGRPCTraining runs StreamTelemetry concurrently with StartTraining (see grpcrunner).
func (m *manager) runNativeGRPCTraining(ctx context.Context, id string, cfg *trainingrpc.TrainingConfig, rec *runRecord) {
	exitCode := 0
	runErr := false
	defer func() {
		if rec.procExited != nil {
			select {
			case <-rec.procExited:
			default:
				close(rec.procExited)
			}
		}
		for _, p := range rec.configCleanups {
			if p != "" {
				_ = os.Remove(p)
			}
		}
		m.mu.Lock()
		if r := m.byID[id]; r != nil {
			r.Running = false
			r.Error = runErr
			r.ExitCode = exitCode
		}
		m.mu.Unlock()
		ex := m.exitCode(id)
		wsHub.broadcast(id, map[string]any{
			"type":      "lifecycle",
			"run_id":    id,
			"state":     "finished",
			"ts":        time.Now().UTC().Format(time.RFC3339),
			"exit_code": ex,
		})
		if rec.RunTarget == "runpod" && rec.RunpodDestroyOnExit {
			dctx, cancel := context.WithTimeout(context.Background(), 20*time.Minute)
			defer cancel()
			out, derr := runpodDestroy(dctx, rec.RunpodVarFile)
			m.appendLogSubprocessAware(id, []byte("\n=== runpod: OpenTofu destroy ===\n"), "stderr")
			m.appendLogSubprocessAware(id, []byte(out), "stderr")
			if derr != nil {
				m.appendLogSubprocessAware(id, []byte("\n=== runpod destroy error: "+derr.Error()+" ===\n"), "stderr")
			}
		}
	}()

	conn, err := trainingGRPCEnsureConn(ctx)
	if err != nil {
		m.appendLogSubprocessAware(id, []byte("[native:gRPC] dial failed: "+err.Error()+"\n"), "stderr")
		exitCode = 1
		runErr = true
		return
	}

	m.appendLogSubprocessAware(id, []byte("[native:gRPC] StartTraining "+trainingGRPCAddressResolved()+"\n"), "stderr")

	defer func() {
		m.mu.Lock()
		if r := m.byID[id]; r != nil {
			r.nativeStreamCancel = nil
		}
		m.mu.Unlock()
	}()

	hooks := &grpcrunner.Hooks{
		RunID: id,
		OnLog: func(msg string) {
			m.appendLogSubprocessAware(id, []byte(msg), "stderr")
		},
		OnTelemetry: func(ev *trainingrpc.TelemetryEvent) {
			m.broadcastTelemetryProto(id, ev)
		},
		OnStartResponse: func(st *trainingrpc.StartTrainingResponse) {
			if logMem := formatStartTrainingMemoryLog(st.GetMemoryEstimate()); logMem != "" {
				m.appendLogSubprocessAware(id, []byte(logMem), "stderr")
			}
		},
		OnTrainingAccepted: func(cfg *trainingrpc.TrainingConfig) {
			wsHub.broadcast(id, map[string]any{
				"type":             "xpu_mem",
				"run_id":           id,
				"ts":               time.Now().UTC().Format(time.RFC3339),
				"phase":            "native_engine",
				"epoch":            0,
				"epochs_total":     int(cfg.GetEpochs()),
				"batches":          0,
				"device":           "C++ gRPC",
				"telemetry_source": telemetrySourceGRPCCPP,
			})
		},
		RegisterStreamCancel: func(cancel context.CancelFunc) {
			m.mu.Lock()
			if r := m.byID[id]; r != nil {
				r.nativeStreamCancel = cancel
			}
			m.mu.Unlock()
		},
	}

	exitCode, runErr = grpcrunner.RunTrainingSession(ctx, conn, cfg, hooks)
}

func (m *manager) appendLogSubprocessAware(id string, p []byte, stream string) {
	m.mu.Lock()
	rec := m.byID[id]
	m.mu.Unlock()
	if rec == nil {
		return
	}
	rec.logMu.Lock()
	rec.logBuf.Write(p)
	if rec.cmd != nil {
		m.processTelemetryLinesLocked(id, rec, p, stream)
	}
	rec.logMu.Unlock()
}
