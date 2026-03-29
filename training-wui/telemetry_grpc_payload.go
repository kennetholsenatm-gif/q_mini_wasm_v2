package main

import (
	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/trainingrpc"
)

// buildGRPCMetricWebSocketPayload maps a C++ TelemetryEvent to the JSON object sent on the
// run WebSocket as type "metric". Preserves legacy keys (epoch, mean_loss, mean_mse, line) for charts.
func buildGRPCMetricWebSocketPayload(runID string, ev *trainingrpc.TelemetryEvent, tsRFC3339 string) map[string]any {
	if ev == nil {
		return map[string]any{
			"type":             "metric",
			"run_id":           runID,
			"ts":               tsRFC3339,
			"telemetry_source": telemetrySourceGRPCCPP,
			"engine":           "grpc",
		}
	}
	epoch := float64(ev.GetEpoch())
	trainLoss := ev.GetTrainLoss()
	valLoss := ev.GetValLoss()
	msg := ev.GetMessage()
	return map[string]any{
		"type":                 "metric",
		"run_id":               runID,
		"ts":                   tsRFC3339,
		"epoch":                epoch,
		"mean_loss":            trainLoss,
		"mean_return":          0.0,
		"mean_mse":             valLoss,
		"line":                 "grpc:" + msg,
		"telemetry_source":     telemetrySourceGRPCCPP,
		"engine":               "grpc",
		"unix_ms":              ev.GetUnixMs(),
		"step":                 ev.GetStep(),
		"train_loss":           trainLoss,
		"val_loss":             valLoss,
		"learning_rate":        ev.GetLearningRate(),
		"samples_per_second":   ev.GetSamplesPerSecond(),
		"sampler_queue_depth":  ev.GetSamplerQueueDepth(),
		"prefetch_queue_depth": ev.GetPrefetchQueueDepth(),
		"compute_queue_depth":  ev.GetComputeQueueDepth(),
		"taxonomy_tier":        ev.GetTaxonomyTier(),
		"precision_mode":       ev.GetPrecisionMode(),
		"stage":                ev.GetStage(),
		"event_type":           ev.GetEventType(),
		"graph_id":             ev.GetGraphId(),
		"node_id":              ev.GetNodeId(),
		"enclave_state":        ev.GetEnclaveState(),
		"attestation_state":    ev.GetAttestationState(),
		"decoherence_score":    ev.GetDecoherenceScore(),
		"grpc_message":         msg,
		"epoch_wall_s":         ev.GetEpochWallS(),
		"epoch_batch_count":    ev.GetEpochBatchCount(),
		"epoch_sample_count":   ev.GetEpochSampleCount(),
		"epoch_mean_samples_per_s": ev.GetEpochMeanSamplesPerS(),
		"host_rss_mib":         ev.GetHostRssMib(),
		"estimated_tpem_mib":   ev.GetEstimatedTpemMib(),
		"tier_cap_mib":         ev.GetTierCapMib(),
	}
}
