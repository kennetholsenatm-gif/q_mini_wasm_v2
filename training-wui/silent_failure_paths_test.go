package main

import (
	"context"
	"net/http"
	"net/http/httptest"
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
