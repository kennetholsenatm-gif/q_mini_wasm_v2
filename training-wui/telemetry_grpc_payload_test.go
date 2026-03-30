package main

import (
	"testing"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingrpc"
)

func TestBuildGRPCMetricWebSocketPayload_fullEvent(t *testing.T) {
	ev := &trainingrpc.TelemetryEvent{
		UnixMs:                 1_700_000_000_000,
		Epoch:                  3,
		Step:                   42,
		TrainLoss:              0.5,
		ValLoss:                0.6,
		LearningRate:           1e-4,
		SamplesPerSecond:       123.4,
		SamplerQueueDepth:      1,
		PrefetchQueueDepth:     2,
		ComputeQueueDepth:      3,
		TaxonomyTier:           "L2",
		PrecisionMode:          "bf16",
		Stage:                  "forward",
		EventType:              "step",
		Message:                "ok",
		GraphId:                "g1",
		NodeId:                 "n1",
		EnclaveState:           "ready",
		AttestationState:       "verified",
		DecoherenceScore:       0.001,
		TrainingPhase:          "mopd",
		CascadePolicyOptimizer: "grpo",
	}
	m := buildGRPCMetricWebSocketPayload("run-xyz", ev, "2026-03-27T12:00:00Z")

	expectStr(t, m, "type", "metric")
	expectStr(t, m, "run_id", "run-xyz")
	expectStr(t, m, "ts", "2026-03-27T12:00:00Z")
	expectStr(t, m, "telemetry_source", telemetrySourceGRPCCPP)
	expectStr(t, m, "engine", "grpc")
	expectFloat(t, m, "epoch", 3)
	expectFloat(t, m, "mean_loss", 0.5)
	expectFloat(t, m, "mean_mse", 0.6)
	expectFloat(t, m, "mean_return", 0)
	expectStr(t, m, "line", "grpc:ok")
	expectUint64(t, m, "unix_ms", 1_700_000_000_000)
	expectUint32(t, m, "step", 42)
	expectFloat(t, m, "train_loss", 0.5)
	expectFloat(t, m, "val_loss", 0.6)
	expectFloat(t, m, "learning_rate", 1e-4)
	expectFloat(t, m, "samples_per_second", 123.4)
	expectUint32(t, m, "sampler_queue_depth", 1)
	expectUint32(t, m, "prefetch_queue_depth", 2)
	expectUint32(t, m, "compute_queue_depth", 3)
	expectStr(t, m, "taxonomy_tier", "L2")
	expectStr(t, m, "precision_mode", "bf16")
	expectStr(t, m, "stage", "forward")
	expectStr(t, m, "event_type", "step")
	expectStr(t, m, "graph_id", "g1")
	expectStr(t, m, "node_id", "n1")
	expectStr(t, m, "enclave_state", "ready")
	expectStr(t, m, "attestation_state", "verified")
	expectFloat(t, m, "decoherence_score", 0.001)
	expectStr(t, m, "grpc_message", "ok")
	expectFloat(t, m, "epoch_wall_s", 0)
	expectUint32(t, m, "epoch_batch_count", 0)
	expectUint64(t, m, "epoch_sample_count", 0)
	expectFloat(t, m, "epoch_mean_samples_per_s", 0)
	expectFloat(t, m, "host_rss_mib", 0)
	expectFloat(t, m, "estimated_tpem_mib", 0)
	expectFloat(t, m, "tier_cap_mib", 0)
	expectStr(t, m, "training_phase", "mopd")
	expectStr(t, m, "cascade_policy_optimizer", "grpo")
}

func TestBuildGRPCMetricWebSocketPayload_nilEvent(t *testing.T) {
	m := buildGRPCMetricWebSocketPayload("run-0", nil, "2026-03-27T12:00:01Z")
	expectStr(t, m, "type", "metric")
	expectStr(t, m, "run_id", "run-0")
	expectStr(t, m, "ts", "2026-03-27T12:00:01Z")
	expectStr(t, m, "telemetry_source", telemetrySourceGRPCCPP)
	expectStr(t, m, "engine", "grpc")
	if _, ok := m["epoch"]; ok {
		t.Fatalf("nil event should omit epoch, got %v", m["epoch"])
	}
}

func expectStr(t *testing.T, m map[string]any, key, want string) {
	t.Helper()
	v, ok := m[key]
	if !ok {
		t.Fatalf("missing key %q", key)
	}
	got, ok := v.(string)
	if !ok {
		t.Fatalf("key %q: want string, got %T (%v)", key, v, v)
	}
	if got != want {
		t.Fatalf("key %q: got %q, want %q", key, got, want)
	}
}

func expectFloat(t *testing.T, m map[string]any, key string, want float64) {
	t.Helper()
	v, ok := m[key]
	if !ok {
		t.Fatalf("missing key %q", key)
	}
	got, ok := v.(float64)
	if !ok {
		t.Fatalf("key %q: want float64, got %T (%v)", key, v, v)
	}
	if got != want {
		t.Fatalf("key %q: got %v, want %v", key, got, want)
	}
}

func expectUint32(t *testing.T, m map[string]any, key string, want uint32) {
	t.Helper()
	v, ok := m[key]
	if !ok {
		t.Fatalf("missing key %q", key)
	}
	got, ok := v.(uint32)
	if !ok {
		t.Fatalf("key %q: want uint32, got %T (%v)", key, v, v)
	}
	if got != want {
		t.Fatalf("key %q: got %v, want %v", key, got, want)
	}
}

func expectUint64(t *testing.T, m map[string]any, key string, want uint64) {
	t.Helper()
	v, ok := m[key]
	if !ok {
		t.Fatalf("missing key %q", key)
	}
	got, ok := v.(uint64)
	if !ok {
		t.Fatalf("key %q: want uint64, got %T (%v)", key, v, v)
	}
	if got != want {
		t.Fatalf("key %q: got %v, want %v", key, got, want)
	}
}
