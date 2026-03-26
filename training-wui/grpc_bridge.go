package main

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"time"
)

const (
	trainingGRPCBridgeEnv = "QMINIWASM_WUI_GRPC_BRIDGE"
	trainingGRPCAddrEnv   = "QMINIWASM_TRAINING_GRPC_ADDR"
	defaultTrainingGRPC   = "127.0.0.1:50061"
	trainingRPCMethod     = "qminiwasm.trainingrpc.TrainingEngineService/StreamTelemetry"
)

func (m *manager) maybeStartGRPCTelemetryBridge(id string, rec *runRecord, opts runStartOpts) {
	if !grpcBridgeEnabled() {
		return
	}
	if !runTargetCanUseLocalBridge(opts) {
		return
	}
	if _, err := exec.LookPath("grpcurl"); err != nil {
		m.appendLog(id, []byte("=== grpc bridge disabled: grpcurl not found on PATH ===\n"), "stderr")
		return
	}
	ctx, cancel := context.WithCancel(context.Background())
	m.mu.Lock()
	if r := m.byID[id]; r != nil {
		r.grpcBridgeCancel = cancel
	}
	m.mu.Unlock()
	go m.runGRPCTelemetryBridge(ctx, id)
}

func grpcBridgeEnabled() bool {
	v := strings.ToLower(strings.TrimSpace(os.Getenv(trainingGRPCBridgeEnv)))
	return v == "1" || v == "true" || v == "yes" || v == "on"
}

func runTargetCanUseLocalBridge(opts runStartOpts) bool {
	if opts.RunTarget == "local" {
		return true
	}
	if opts.RunTarget == "runpod" && !opts.RunpodTrainOnPod {
		return true
	}
	return false
}

func (m *manager) runGRPCTelemetryBridge(ctx context.Context, runID string) {
	addr := strings.TrimSpace(os.Getenv(trainingGRPCAddrEnv))
	if addr == "" {
		addr = defaultTrainingGRPC
	}
	reqBody := fmt.Sprintf(`{"run_id":"%s","include_debug":true}`, runID)
	cmd := exec.CommandContext(
		ctx,
		"grpcurl",
		"-plaintext",
		"-d",
		reqBody,
		addr,
		trainingRPCMethod,
	)
	cmd.Dir = repoRoot

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		m.appendLog(runID, []byte("=== grpc bridge failed: "+err.Error()+" ===\n"), "stderr")
		return
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		m.appendLog(runID, []byte("=== grpc bridge failed: "+err.Error()+" ===\n"), "stderr")
		return
	}
	if err := cmd.Start(); err != nil {
		m.appendLog(runID, []byte("=== grpc bridge failed to start: "+err.Error()+" ===\n"), "stderr")
		return
	}

	m.appendLog(runID, []byte("=== grpc bridge connected: "+addr+" ===\n"), "stderr")
	go m.pipeGRPCBridgeStderr(runID, stderr)
	m.consumeGRPCBridgeJSON(runID, stdout)
	_ = cmd.Wait()
}

func (m *manager) pipeGRPCBridgeStderr(runID string, r io.Reader) {
	sc := bufio.NewScanner(r)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" {
			continue
		}
		m.appendLog(runID, []byte("[grpc] "+line+"\n"), "stderr")
	}
}

func (m *manager) consumeGRPCBridgeJSON(runID string, r io.Reader) {
	dec := json.NewDecoder(r)
	for {
		var evt map[string]any
		if err := dec.Decode(&evt); err != nil {
			return
		}
		epoch := grpcNum(evt, "epoch")
		trainLoss := grpcNum(evt, "train_loss", "trainLoss")
		valLoss := grpcNum(evt, "val_loss", "valLoss")
		msg := grpcString(evt, "message")
		eventType := strings.ToLower(grpcString(evt, "event_type", "eventType"))

		wsHub.broadcast(runID, map[string]any{
			"type":        "metric",
			"run_id":      runID,
			"ts":          time.Now().UTC().Format(time.RFC3339),
			"epoch":       epoch,
			"mean_loss":   trainLoss,
			"mean_return": 0.0,
			"mean_mse":    valLoss,
			"line":        "grpc:" + msg,
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
	}
}

func grpcNum(m map[string]any, keys ...string) float64 {
	for _, k := range keys {
		v, ok := m[k]
		if !ok {
			continue
		}
		switch x := v.(type) {
		case float64:
			return x
		case int:
			return float64(x)
		case int64:
			return float64(x)
		case json.Number:
			if f, err := x.Float64(); err == nil {
				return f
			}
		case string:
			if f, err := strconv.ParseFloat(strings.TrimSpace(x), 64); err == nil {
				return f
			}
		}
	}
	return 0
}

func grpcString(m map[string]any, keys ...string) string {
	for _, k := range keys {
		v, ok := m[k]
		if !ok || v == nil {
			continue
		}
		return strings.TrimSpace(fmt.Sprintf("%v", v))
	}
	return ""
}
