// Command qmw-grpc-train runs one training job via C++ TrainingEngineService (gRPC).
// Used on RunPod and locally when the WUI is not the process parent.
package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/grpcrunner"
	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingconfig"
	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingrpc"
)

func main() {
	log.SetFlags(0)
	log.SetOutput(os.Stderr)

	root := flag.String("root", ".", "repository root (absolute or cwd-relative)")
	config := flag.String("config", "", "training TOML path relative to root or absolute")
	grpcAddr := flag.String("grpc", "127.0.0.1:50061", "TrainingEngineService address")
	runID := flag.String("run-id", "", "run id (default: env QMW_TRAINING_RUN_ID or generated)")
	stopFile := flag.String("stop-file", "", "optional path; when file exists, cooperative StopTraining is sent")
	flag.Parse()

	repoRoot, err := filepath.Abs(strings.TrimSpace(*root))
	if err != nil {
		log.Fatal(err)
	}
	cfgRel := strings.TrimSpace(*config)
	if cfgRel == "" {
		log.Fatal("-config is required")
	}
	absConfig := cfgRel
	if !filepath.IsAbs(cfgRel) {
		absConfig = filepath.Join(repoRoot, filepath.FromSlash(cfgRel))
	}
	rid := strings.TrimSpace(*runID)
	if rid == "" {
		rid = strings.TrimSpace(os.Getenv("QMW_TRAINING_RUN_ID"))
	}
	if rid == "" {
		rid = fmt.Sprintf("cli-%d", os.Getpid())
	}

	cfg, err := trainingconfig.TrainingTOMLToProto(absConfig, rid, repoRoot)
	if err != nil {
		log.Fatal(err)
	}

	addr := strings.TrimSpace(*grpcAddr)
	if addr == "" {
		addr = "127.0.0.1:50061"
	}
	conn, err := grpc.NewClient(addr, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		log.Fatal(err)
	}
	defer conn.Close()

	ctx := context.Background()
	h := &grpcrunner.Hooks{
		RunID: rid,
		OnLog: func(msg string) {
			_, _ = os.Stderr.WriteString(msg)
		},
		OnTelemetry: func(ev *trainingrpc.TelemetryEvent) {
			// Line-oriented logs for SSH / journald consumers
			msg := strings.TrimSpace(ev.GetMessage())
			if msg != "" {
				_, _ = fmt.Fprintf(os.Stdout, "%s\n", msg)
			}
		},
		StopFile: strings.TrimSpace(*stopFile),
	}

	code, runErr := grpcrunner.RunTrainingSession(ctx, conn, cfg, h)
	if runErr || code != 0 {
		if code == 0 {
			code = 1
		}
		os.Exit(code)
	}
	os.Exit(0)
}
