package main

import (
	"context"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestRunpodServerlessDoJSONInvalidJSON(t *testing.T) {
	t.Setenv("RUNPOD_TOKEN_END", "test-token")
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte("{invalid-json"))
	}))
	defer srv.Close()

	_, _, _, err := runpodServerlessDoJSON(context.Background(), http.MethodGet, srv.URL, nil)
	if err == nil {
		t.Fatal("expected JSON decode error")
	}
}

func TestParseServerlessWorkerOutcome(t *testing.T) {
	out := map[string]any{
		"output": map[string]any{
			"ok":        false,
			"exit_code": float64(13),
			"stderr":    "boom",
		},
	}
	ok, exitCode, stderrMsg, hasOutput := parseServerlessWorkerOutcome(out)
	if !hasOutput {
		t.Fatal("expected output payload")
	}
	if ok {
		t.Fatal("expected ok=false")
	}
	if exitCode != 13 {
		t.Fatalf("expected exitCode=13, got %d", exitCode)
	}
	if stderrMsg != "boom" {
		t.Fatalf("expected stderr boom, got %q", stderrMsg)
	}
}

func TestCanonicalMetricLineUpdatesRunState(t *testing.T) {
	m := newManager()
	rec := &runRecord{ID: "r1"}
	m.byID["r1"] = rec

	m.handleLogLine("r1", "qmw_metric epoch=2 mean_loss=1.500000 mean_return=0.000000 mean_mse=1.500000", "stdout")
	if rec.LastEpoch != 2 {
		t.Fatalf("expected LastEpoch=2 got %d", rec.LastEpoch)
	}
	if rec.LastMeanLoss != 1.5 {
		t.Fatalf("expected LastMeanLoss=1.5 got %f", rec.LastMeanLoss)
	}
	if rec.LastMeanMSE != 1.5 {
		t.Fatalf("expected LastMeanMSE=1.5 got %f", rec.LastMeanMSE)
	}
}

func TestCanonicalMetricLineOnStderrUpdatesRunState(t *testing.T) {
	m := newManager()
	rec := &runRecord{ID: "r5"}
	m.byID["r5"] = rec
	line := "INFO qminiwasm.training.loop: qmw_metric epoch=4 mean_loss=1.000000 mean_return=0.000000 mean_mse=1.000000"
	m.handleLogLine("r5", line, "stderr")
	if rec.LastEpoch != 4 {
		t.Fatalf("expected LastEpoch=4 on stderr, got %d", rec.LastEpoch)
	}
}

func TestCooperativeStopWritesLocalStopFile(t *testing.T) {
	m := newManager()
	tmp := t.TempDir()
	stopPath := filepath.Join(tmp, "stop_coop")
	rec := &runRecord{
		ID:          "coop1",
		Running:     true,
		wuiStopFile: stopPath,
		cmd:         &exec.Cmd{},
	}
	m.byID["coop1"] = rec
	if err := m.cooperativeStop("coop1"); err != nil {
		t.Fatal(err)
	}
	b, err := os.ReadFile(stopPath)
	if err != nil {
		t.Fatal(err)
	}
	if string(b) != "1\n" {
		t.Fatalf("stop file content %q, want \"1\\n\"", string(b))
	}
}

func TestFlushTelemetryLinesParsesPartialLine(t *testing.T) {
	m := newManager()
	rec := &runRecord{ID: "r2"}
	rec.stdoutLineBuf.WriteString("qmw_metric epoch=3 mean_loss=2.000000 mean_return=0.000000 mean_mse=2.000000")
	m.byID["r2"] = rec

	m.flushTelemetryLinesLocked("r2", rec)
	if rec.LastEpoch != 3 {
		t.Fatalf("expected LastEpoch=3 got %d", rec.LastEpoch)
	}
}

func TestRunWSHubBroadcastSkipsClosedSubscriber(t *testing.T) {
	h := newRunWSHub()
	sub := &runWSSub{send: make(chan []byte, 1), done: make(chan struct{})}
	close(sub.done)
	h.subs["run-1"] = map[*runWSSub]struct{}{sub: {}}
	h.broadcast("run-1", map[string]any{"type": "metric"})
}

func TestBackendStatusLineUpdatesRunState(t *testing.T) {
	m := newManager()
	rec := &runRecord{ID: "r3"}
	m.byID["r3"] = rec
	line := "qmw_backend_status backend=xpu requested_accelerator=sycl selected_device=cpu reason_code=sycl_unavailable_fallback_cpu available=0 support_class=unsupported device_name=unknown strict_xpu=0 reason=missing_runtime sycl_active=0 sycl_backend=dpctl_sycl sycl_device=none sycl_fallback=no_default_device sycl_dpctl_count=0"
	m.handleLogLine("r3", line, "stdout")
	if rec.BackendRequested != "sycl" {
		t.Fatalf("expected backend requested sycl, got %q", rec.BackendRequested)
	}
	if rec.BackendSelected != "cpu" {
		t.Fatalf("expected backend selected cpu, got %q", rec.BackendSelected)
	}
	if rec.SYCLActive {
		t.Fatalf("expected sycl inactive")
	}
	if rec.SYCLBackend != "dpctl_sycl" {
		t.Fatalf("expected sycl backend dpctl_sycl, got %q", rec.SYCLBackend)
	}
}

func TestRoutingTelemetryLineUpdatesRunState(t *testing.T) {
	m := newManager()
	rec := &runRecord{ID: "r4"}
	m.byID["r4"] = rec
	m.handleLogLine("r4", "qmw_routing_telemetry routing_state=1 assignment_ms=12.400 budget_ms=50", "stdout")
	if rec.LastRoutingState != 1 || rec.LastAssignmentMS != 12.4 || rec.RoutingBudgetMS != 50 {
		t.Fatalf("routing telemetry: got state=%d assign=%f budget=%f", rec.LastRoutingState, rec.LastAssignmentMS, rec.RoutingBudgetMS)
	}
	m.handleLogLine("r4", "qmw_routing_handoff from=1 to=2 assignment_ms=52.300 budget_ms=50 reason=latency_budget combinatorial_wall=1", "stdout")
	if rec.LastRoutingState != 2 || rec.LastAssignmentMS != 52.3 {
		t.Fatalf("routing handoff: got state=%d assign=%f", rec.LastRoutingState, rec.LastAssignmentMS)
	}
}

func TestXPUMemTrainingSetupLineParses(t *testing.T) {
	line := "qmw_xpu_mem phase=training_setup device=xpu:0 batch_size=64 train_samples=1000 dataloader_workers=2"
	mm := xpuMemTrainingSetupRe.FindStringSubmatch(line)
	if len(mm) != 5 || mm[1] != "xpu:0" || mm[2] != "64" || mm[3] != "1000" || mm[4] != "2" {
		t.Fatalf("training_setup regex: %+v", mm)
	}
}

func TestXPUMemEpochLineParses(t *testing.T) {
	line := "qmw_xpu_mem phase=epoch_end epoch=1 epochs_total=50 batches=10 device=xpu:0 allocated_bytes=1048576 max_allocated_bytes=2097152 allocated_mib=1.0000 max_allocated_mib=2.0000"
	mm := xpuMemEpochRe.FindStringSubmatch(line)
	if len(mm) != 10 || mm[1] != "epoch_end" || mm[2] != "1" || mm[5] != "0" {
		t.Fatalf("epoch xpu_mem regex: %+v", mm)
	}
}

func TestTrainThroughputLineParses(t *testing.T) {
	line := "qmw_train_throughput epoch=2 epochs_total=50 wall_s=1.2500 batches=8 samples=512 batch_size=64 samples_per_s=409.6000 batches_per_s=6.4000 dataloader_workers=0 cascade_s=0.0500 host_rss_mib=8192.00"
	mm := trainThroughputRe.FindStringSubmatch(line)
	if len(mm) < 12 || mm[1] != "2" || mm[4] != "8" || mm[10] != "0.0500" {
		t.Fatalf("train_throughput regex: len=%d %+v", len(mm), mm)
	}
	if mm[11] != "8192.00" {
		t.Fatalf("expected host_rss_mib group, got %q", mm[11])
	}
	lineNoRSS := "qmw_train_throughput epoch=1 epochs_total=10 wall_s=2.0000 batches=4 samples=256 batch_size=64 samples_per_s=128.0000 batches_per_s=2.0000 dataloader_workers=1 cascade_s=0.0000"
	mm2 := trainThroughputRe.FindStringSubmatch(lineNoRSS)
	if len(mm2) < 11 || mm2[1] != "1" {
		t.Fatalf("train_throughput no rss: %+v", mm2)
	}
}
