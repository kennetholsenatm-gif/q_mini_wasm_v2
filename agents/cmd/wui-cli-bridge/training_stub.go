//go:build !cgo
// +build !cgo

package main

import "fmt"

// Stub implementations when CGO is not available

func runtimeCGOEnabled() bool {
	return false
}

type TrainingPipelineCGO struct {
	initialized bool
	config      PipelineConfigCGO
	state       string
}

type PipelineConfigCGO struct {
	AcquisitionThreads    int
	NumExperts            int
	GraphNodes            int
	EnableBettiGuidance   bool
	EnableKnowledgeEngine bool
	Epochs                int
	BatchSize             int
}

type PipelineMetricsCGO struct {
	FFPositiveGoodness  uint32
	FFNegativeGoodness  uint32
	FFGoodnessDelta     int32
	MoELoadBalanceScore float32
	BettiBeta0          uint32
	BettiBeta1          uint32
	BettiBeta2          uint32
	GraphNodes          int
	GraphEdges          int
	DSTotalAcquired     int
	DSTotalPerturbed    int
	CurrentEpoch        uint64
	CurrentBatch        uint64
	TrainingProgress    float32
	IsRunning           bool
	StatusMessage       string
}

func NewTrainingPipelineCGO(numExperts, graphNodes int, enableBettiGuidance bool) (*TrainingPipelineCGO, error) {
	return &TrainingPipelineCGO{
		initialized: true,
		state:       "ready",
		config: PipelineConfigCGO{
			NumExperts:          numExperts,
			GraphNodes:          graphNodes,
			EnableBettiGuidance: enableBettiGuidance,
		},
	}, nil
}

func (p *TrainingPipelineCGO) Initialize(config PipelineConfigCGO) error {
	p.config = config
	p.state = "initialized"
	return nil
}

func (p *TrainingPipelineCGO) GetState() string {
	return p.state
}

func (p *TrainingPipelineCGO) Start() error {
	return fmt.Errorf("training pipeline requires CGO-enabled build")
}

func (p *TrainingPipelineCGO) Stop() {
	p.state = "stopped"
}

func (p *TrainingPipelineCGO) Pause() error {
	return fmt.Errorf("training pipeline requires CGO-enabled build")
}

func (p *TrainingPipelineCGO) Resume() error {
	return fmt.Errorf("training pipeline requires CGO-enabled build")
}

func (p *TrainingPipelineCGO) GetMetrics() (*PipelineMetricsCGO, error) {
	return nil, fmt.Errorf("training pipeline metrics require CGO-enabled build")
}

func (p *TrainingPipelineCGO) ApplyBettiGuidance(force bool) error {
	_ = force
	return fmt.Errorf("betti guidance requires CGO-enabled build")
}

func (p *TrainingPipelineCGO) Export(path string, includeTopology bool) error {
	_, _ = path, includeTopology
	return fmt.Errorf("model export requires CGO-enabled build")
}

func (p *TrainingPipelineCGO) Import(path string) error {
	_ = path
	return fmt.Errorf("model import requires CGO-enabled build")
}
