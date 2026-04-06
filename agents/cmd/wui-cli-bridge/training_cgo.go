package main

/*
#cgo CFLAGS: -I../../../q_mini_wasm_v2/dll
#cgo LDFLAGS: -L../../../q_mini_wasm_v2/build/bin -lq_mini_wasm_v2

#include "q_mini_wasm_v2_api.hpp"
#include <stdlib.h>
*/
import "C"
import (
	"encoding/json"
	"fmt"
	"unsafe"
)

// TrainingPipelineCGO provides CGO bindings to C++ training pipeline
type TrainingPipelineCGO struct {
	handle unsafe.Pointer
}

// PipelineConfigCGO matches C++ PipelineConfig
 type PipelineConfigCGO struct {
	AcquisitionThreads      int     `json:"acquisition_threads"`
	NumExperts             int     `json:"num_experts"`
	GraphNodes             int     `json:"graph_nodes"`
	EnableBettiGuidance    bool    `json:"enable_betti_guidance"`
	EnableKnowledgeEngine  bool    `json:"enable_knowledge_engine"`
	Epochs                 int     `json:"epochs"`
	BatchSize              int     `json:"batch_size"`
}

// PipelineMetricsCGO matches C++ PipelineMetrics
 type PipelineMetricsCGO struct {
	FFPositiveGoodness uint32  `json:"ff_positive_goodness"`
	FFNegativeGoodness uint32  `json:"ff_negative_goodness"`
	FFGoodnessDelta    int32   `json:"ff_goodness_delta"`
	MoELoadBalanceScore float32 `json:"moe_load_balance_score"`
	BettiBeta0         uint32  `json:"betti_beta_0"`
	BettiBeta1         uint32  `json:"betti_beta_1"`
	BettiBeta2         uint32  `json:"betti_beta_2"`
	GraphNodes         int     `json:"graph_nodes"`
	GraphEdges         int     `json:"graph_edges"`
	DSTotalAcquired    int     `json:"ds_total_acquired"`
	DSTotalPerturbed   int     `json:"ds_total_perturbed"`
	CurrentEpoch       uint64  `json:"current_epoch"`
	CurrentBatch       uint64  `json:"current_batch"`
	TrainingProgress   float32 `json:"training_progress"`
	IsRunning          bool    `json:"is_running"`
	StatusMessage      string  `json:"status_message"`
}

// NewTrainingPipelineCGO creates a new training pipeline via CGO
func NewTrainingPipelineCGO(numExperts, graphNodes int, enableBettiGuidance bool) (*TrainingPipelineCGO, error) {
	bettiInt := 0
	if enableBettiGuidance {
		bettiInt = 1
	}
	
	handle := C.training_pipeline_create(
		C.size_t(numExperts),
		C.size_t(graphNodes),
		C.int(bettiInt),
	)
	
	if handle == nil {
		return nil, fmt.Errorf("failed to create training pipeline")
	}
	
	return &TrainingPipelineCGO{handle: handle}, nil
}

// Destroy destroys the training pipeline
func (p *TrainingPipelineCGO) Destroy() {
	if p.handle != nil {
		C.training_pipeline_destroy(p.handle)
		p.handle = nil
	}
}

// Initialize initializes the pipeline with config
func (p *TrainingPipelineCGO) Initialize(config PipelineConfigCGO) error {
	result := C.training_pipeline_initialize(
		p.handle,
		C.size_t(config.AcquisitionThreads),
		C.size_t(config.Epochs),
		C.size_t(config.BatchSize),
	)
	
	if result != 0 {
		return fmt.Errorf("failed to initialize pipeline, error code: %d", result)
	}
	return nil
}

// Start starts the training loop
func (p *TrainingPipelineCGO) Start() error {
	result := C.training_pipeline_start(p.handle)
	if result != 0 {
		return fmt.Errorf("failed to start training, error code: %d", result)
	}
	return nil
}

// Stop stops the training loop
func (p *TrainingPipelineCGO) Stop() {
	C.training_pipeline_stop(p.handle)
}

// Pause pauses training
func (p *TrainingPipelineCGO) Pause() error {
	result := C.training_pipeline_pause(p.handle)
	if result != 0 {
		return fmt.Errorf("failed to pause training, error code: %d", result)
	}
	return nil
}

// Resume resumes training
func (p *TrainingPipelineCGO) Resume() error {
	result := C.training_pipeline_resume(p.handle)
	if result != 0 {
		return fmt.Errorf("failed to resume training, error code: %d", result)
	}
	return nil
}

// GetMetrics retrieves training metrics
func (p *TrainingPipelineCGO) GetMetrics() (*PipelineMetricsCGO, error) {
	buffer := make([]byte, 4096)
	
	size := C.training_pipeline_get_metrics(
		p.handle,
		(*C.char)(unsafe.Pointer(&buffer[0])),
		C.size_t(len(buffer)),
	)
	
	if size == 0 {
		return nil, fmt.Errorf("failed to get metrics")
	}
	
	var metrics PipelineMetricsCGO
	if err := json.Unmarshal(buffer[:size], &metrics); err != nil {
		return nil, fmt.Errorf("failed to parse metrics: %w", err)
	}
	
	return &metrics, nil
}

// ApplyBettiGuidance triggers topology optimization
func (p *TrainingPipelineCGO) ApplyBettiGuidance(force bool) error {
	forceInt := 0
	if force {
		forceInt = 1
	}
	
	result := C.training_pipeline_apply_betti_guidance(p.handle, C.int(forceInt))
	if result != 0 {
		return fmt.Errorf("failed to apply Betti guidance, error code: %d", result)
	}
	return nil
}

// Export exports model to file
func (p *TrainingPipelineCGO) Export(path string, includeTopology bool) error {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))
	
	includeInt := 0
	if includeTopology {
		includeInt = 1
	}
	
	result := C.training_pipeline_export(p.handle, cPath, C.int(includeInt))
	if result != 0 {
		return fmt.Errorf("failed to export model, error code: %d", result)
	}
	return nil
}

// Import imports model from file
func (p *TrainingPipelineCGO) Import(path string) error {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))
	
	result := C.training_pipeline_import(p.handle, cPath)
	if result != 0 {
		return fmt.Errorf("failed to import model, error code: %d", result)
	}
	return nil
}

// GetState returns current pipeline state
func (p *TrainingPipelineCGO) GetState() string {
	cState := C.training_pipeline_get_state(p.handle)
	return C.GoString(cState)
}
