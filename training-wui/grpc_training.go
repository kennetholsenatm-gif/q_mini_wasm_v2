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
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"strings"
	"sync"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingrpc"
)

const (
	trainingGRPCAddrEnv = "QMINIWASM_TRAINING_GRPC_ADDR"
	trainingRuntimeModeEnv = "QMINIWASM_TRAINING_RUNTIME_MODE" // auto|python|cpp
	defaultTrainingGRPC = "127.0.0.1:50061"
)

var (
	trainingGRPCMu   sync.Mutex
	trainingGRPCConn *grpc.ClientConn
)

func trainingGRPCAddress() string {
	addr := strings.TrimSpace(os.Getenv(trainingGRPCAddrEnv))
	if addr == "" {
		return defaultTrainingGRPC
	}
	return addr
}

func trainingGRPCEnsureConn(ctx context.Context) (*grpc.ClientConn, error) {
	trainingGRPCMu.Lock()
	defer trainingGRPCMu.Unlock()
	if trainingGRPCConn != nil {
		return trainingGRPCConn, nil
	}
	addr := trainingGRPCAddress()
	c, err := grpc.NewClient(addr, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		return nil, err
	}
	trainingGRPCConn = c
	return c, nil
}

func useNativeTrainingEngineGRPC(opts runStartOpts) bool {
	mode := strings.ToLower(strings.TrimSpace(os.Getenv(trainingRuntimeModeEnv)))
	switch mode {
	case "cpp", "grpc", "native":
		return true
	case "python":
		return false
	case "optimized":
		mode = "auto"
	}
	if mode == "auto" {
		// Deterministic fallback: only route to native gRPC when endpoint is reachable now.
		if !trainingGRPCReachable(600 * time.Millisecond) {
			return false
		}
	}
	if opts.RunTarget == "local" {
		return true
	}
	if opts.RunTarget == "runpod" && !opts.RunpodTrainOnPod {
		return true
	}
	return false
}

func trainingGRPCReachable(timeout time.Duration) bool {
	addr := trainingGRPCAddress()
	d := net.Dialer{Timeout: timeout}
	c, err := d.Dial("tcp", addr)
	if err != nil {
		return false
	}
	_ = c.Close()
	return true
}

func (m *manager) broadcastTelemetryProto(runID string, ev *trainingrpc.TelemetryEvent) {
	epoch := float64(ev.GetEpoch())
	trainLoss := ev.GetTrainLoss()
	valLoss := ev.GetValLoss()
	msg := ev.GetMessage()
	eventType := strings.ToLower(ev.GetEventType())

	wsHub.broadcast(runID, map[string]any{
		"type":             "metric",
		"run_id":           runID,
		"ts":               time.Now().UTC().Format(time.RFC3339),
		"epoch":            epoch,
		"mean_loss":        trainLoss,
		"mean_return":      0.0,
		"mean_mse":         valLoss,
		"line":             "grpc:" + msg,
		"telemetry_source": telemetrySourceGRPCCPP,
	})

	m.mu.Lock()
	if rec := m.byID[runID]; rec != nil {
		rec.LastEpoch = int(epoch)
		rec.LastMeanLoss = trainLoss
		rec.LastMeanMSE = valLoss
		rec.LastMeanReturn = 0.0
	}
	m.mu.Unlock()

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
	if strings.TrimSpace(ev.GetEnclaveState()) != "" || strings.TrimSpace(ev.GetAttestationState()) != "" {
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

// runNativeGRPCTraining runs StartTraining + StreamTelemetry until the stream ends or ctx is cancelled.
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
		m.appendLogSubprocessAware(id, []byte("=== gRPC: dial failed: "+err.Error()+" ===\n"), "stderr")
		exitCode = 1
		runErr = true
		return
	}
	cli := trainingrpc.NewTrainingEngineServiceClient(conn)

	m.appendLogSubprocessAware(id, []byte("=== gRPC: StartTraining "+trainingGRPCAddress()+" ===\n"), "stderr")
	stResp, err := cli.StartTraining(ctx, &trainingrpc.StartTrainingRequest{Config: cfg})
	if err != nil {
		m.appendLogSubprocessAware(id, []byte("=== gRPC StartTraining error: "+err.Error()+" ===\n"), "stderr")
		exitCode = 1
		runErr = true
		return
	}
	if !stResp.GetAccepted() {
		m.appendLogSubprocessAware(id, []byte("=== gRPC StartTraining rejected: "+stResp.GetMessage()+" ===\n"), "stderr")
		exitCode = 1
		runErr = true
		return
	}
	m.appendLogSubprocessAware(id, []byte("=== gRPC: training accepted ===\n"), "stderr")

	stream, err := cli.StreamTelemetry(ctx, &trainingrpc.TelemetryRequest{RunId: id, IncludeDebug: true})
	if err != nil {
		m.appendLogSubprocessAware(id, []byte("=== gRPC StreamTelemetry error: "+err.Error()+" ===\n"), "stderr")
		exitCode = 1
		runErr = true
		return
	}
	for {
		ev, err := stream.Recv()
		if errors.Is(err, io.EOF) {
			break
		}
		if err != nil {
			if errors.Is(err, context.Canceled) {
				m.appendLogSubprocessAware(id, []byte("=== gRPC telemetry: cancelled ===\n"), "stderr")
			} else {
				m.appendLogSubprocessAware(id, []byte("=== gRPC telemetry recv: "+err.Error()+" ===\n"), "stderr")
				runErr = true
				exitCode = 1
			}
			break
		}
		m.broadcastTelemetryProto(id, ev)
	}

	st, serr := cli.GetStatus(context.Background(), &trainingrpc.StatusRequest{RunId: id})
	if serr == nil && st != nil {
		switch st.GetState() {
		case trainingrpc.StatusResponse_ENGINE_STATE_FAILED:
			runErr = true
			exitCode = 1
		case trainingrpc.StatusResponse_ENGINE_STATE_STOPPED, trainingrpc.StatusResponse_ENGINE_STATE_IDLE:
			if exitCode == 0 {
				exitCode = 0
			}
		}
	}
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
