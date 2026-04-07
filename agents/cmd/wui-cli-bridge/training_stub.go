//go:build !cgo
// +build !cgo

package main

// Stub implementations when CGO is not available

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
