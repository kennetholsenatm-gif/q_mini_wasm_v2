// Package grpcrunner runs a training session against TrainingEngineService (C++).
package grpcrunner

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"
	"sync"
	"time"

	"google.golang.org/grpc"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingrpc"
)

// Hooks customize logging and cooperative stop for a training session.
type Hooks struct {
	RunID string

	// StopFile, if non-empty, is polled; when the file exists, StopTraining is invoked.
	StopFile string

	// OnLog receives human-readable diagnostic lines (no trailing newline required).
	OnLog func(msg string)

	// OnTelemetry receives each telemetry event from StreamTelemetry.
	OnTelemetry func(*trainingrpc.TelemetryEvent)

	// OnTrainingAccepted runs after StartTraining returns accepted (before stream drain completes).
	OnTrainingAccepted func(*trainingrpc.TrainingConfig)

	// OnStartResponse runs after StartTraining returns (accepted or not) for memory estimates etc.
	OnStartResponse func(*trainingrpc.StartTrainingResponse)

	// RegisterStreamCancel receives the cancel func for the telemetry stream (WUI force-stop).
	RegisterStreamCancel func(cancel context.CancelFunc)
}

func logf(h *Hooks, format string, args ...any) {
	if h == nil || h.OnLog == nil {
		return
	}
	h.OnLog(fmt.Sprintf(format, args...))
}

// RunTrainingSession executes StartTraining + StreamTelemetry until completion.
// Caller supplies an established gRPC connection.
func RunTrainingSession(ctx context.Context, conn *grpc.ClientConn, cfg *trainingrpc.TrainingConfig, h *Hooks) (exitCode int, runErr bool) {
	if conn == nil || cfg == nil {
		return 1, true
	}
	id := cfg.GetRunId()
	if h != nil && h.RunID != "" {
		id = h.RunID
	}
	cli := trainingrpc.NewTrainingEngineServiceClient(conn)

	streamCtx, streamCancel := context.WithCancel(context.Background())
	defer streamCancel()
	if h != nil && h.RegisterStreamCancel != nil {
		h.RegisterStreamCancel(streamCancel)
	}

	stopDone := make(chan struct{})
	var stopOnce sync.Once
	if h != nil && strings.TrimSpace(h.StopFile) != "" {
		go func() {
			defer close(stopDone)
			ticker := time.NewTicker(500 * time.Millisecond)
			defer ticker.Stop()
			for {
				select {
				case <-streamCtx.Done():
					return
				case <-ticker.C:
					if _, err := os.Stat(strings.TrimSpace(h.StopFile)); err == nil {
						stopOnce.Do(func() {
							sctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
							defer cancel()
							_, _ = cli.StopTraining(sctx, &trainingrpc.StopTrainingRequest{RunId: id})
						})
						return
					}
				}
			}
		}()
	} else {
		close(stopDone)
	}

	var streamWG sync.WaitGroup
	var streamMu sync.Mutex
	streamExitCode := 0
	streamRunErr := false

	streamWG.Add(1)
	go func() {
		defer streamWG.Done()
		stream, err := cli.StreamTelemetry(streamCtx, &trainingrpc.TelemetryRequest{RunId: id, IncludeDebug: true})
		if err != nil {
			logf(h, "[native:gRPC] StreamTelemetry error: %v\n", err)
			streamMu.Lock()
			streamExitCode = 1
			streamRunErr = true
			streamMu.Unlock()
			return
		}
		for {
			ev, err := stream.Recv()
			if errors.Is(err, io.EOF) {
				break
			}
			if err != nil {
				if errors.Is(err, context.Canceled) {
					logf(h, "[native:gRPC] telemetry stream cancelled\n")
				} else {
					logf(h, "[native:gRPC] telemetry recv error: %v\n", err)
					streamMu.Lock()
					streamRunErr = true
					streamExitCode = 1
					streamMu.Unlock()
				}
				break
			}
			if h != nil && h.OnTelemetry != nil {
				h.OnTelemetry(ev)
			}
		}
	}()

	stResp, err := cli.StartTraining(ctx, &trainingrpc.StartTrainingRequest{Config: cfg})
	if err != nil {
		streamCancel()
		streamWG.Wait()
		logf(h, "[native:gRPC] StartTraining error: %v\n", err)
		return 1, true
	}
	if h != nil && h.OnStartResponse != nil {
		h.OnStartResponse(stResp)
	}
	if !stResp.GetAccepted() {
		streamCancel()
		streamWG.Wait()
		logf(h, "[native:gRPC] StartTraining rejected: %s\n", stResp.GetMessage())
		return 1, true
	}
	logf(h, "[native:gRPC] training accepted\n")
	if h != nil && h.OnTrainingAccepted != nil {
		h.OnTrainingAccepted(cfg)
	}

	streamWG.Wait()
	streamMu.Lock()
	exitCode = streamExitCode
	runErr = streamRunErr
	streamMu.Unlock()

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
	streamCancel()
	<-stopDone
	return exitCode, runErr
}
