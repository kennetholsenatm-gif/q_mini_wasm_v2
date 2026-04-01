package qutrittraining

import (
	"context"
	"fmt"
	"time"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/grpcclient"
	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/qutrittrainingrpc"
)

// Client wraps the qutrit training gRPC service client with enhanced error handling.
type Client struct {
	grpcClient *grpcclient.Client
	client     qutrittrainingrpc.QutritTrainingServiceClient
}

// NewClient creates a new qutrit training gRPC client.
func NewClient(address string) (*Client, error) {
	grpcClient, err := grpcclient.NewClientWithAddress(address)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to %s: %w", address, err)
	}
	return &Client{
		grpcClient: grpcClient,
		client:     qutrittrainingrpc.NewQutritTrainingServiceClient(grpcClient.Conn()),
	}, nil
}

// Close closes the gRPC connection.
func (c *Client) Close() error {
	if c.grpcClient != nil {
		return c.grpcClient.Close()
	}
	return nil
}

// IsConnected checks if the client is connected.
func (c *Client) IsConnected() bool {
	return c.grpcClient != nil && c.grpcClient.IsConnected()
}

// Reconnect attempts to re-establish the connection.
func (c *Client) Reconnect() error {
	if c.grpcClient == nil {
		return fmt.Errorf("client not initialized")
	}
	return c.grpcClient.Reconnect()
}

// ============================================================================
// Phase 1: Quantum Superposition
// ============================================================================

// InitializeSuperposition creates a new tableau with parameters in superposition.
func (c *Client) InitializeSuperposition(ctx context.Context, nParams int32, seed uint64) (int64, error) {
	resp, err := c.client.InitializeSuperposition(ctx, &qutrittrainingrpc.InitializeSuperpositionRequest{
		NParams: nParams,
		Seed:    seed,
	})
	if err != nil {
		return -1, err
	}
	if !resp.Success {
		return -1, fmt.Errorf("initialize superposition failed: %s", resp.Error)
	}
	return resp.TableauId, nil
}

// ApplyHadamardLayer applies qutrit Hadamard gate to create superposition.
func (c *Client) ApplyHadamardLayer(ctx context.Context, tableauID int64, qubits []int32) error {
	resp, err := c.client.ApplyHadamardLayer(ctx, &qutrittrainingrpc.ApplyHadamardLayerRequest{
		TableauId: tableauID,
		Qubits:    qubits,
	})
	if err != nil {
		return err
	}
	if !resp.Success {
		return fmt.Errorf("apply Hadamard layer failed: %s", resp.Error)
	}
	return nil
}

// LatticeCollapse performs projective measurement to finalize weights.
func (c *Client) LatticeCollapse(ctx context.Context, tableauID int64, nMeasurements int32) ([]int32, []float32, error) {
	resp, err := c.client.LatticeCollapse(ctx, &qutrittrainingrpc.LatticeCollapseRequest{
		TableauId:      tableauID,
		NMeasurements:  nMeasurements,
	})
	if err != nil {
		return nil, nil, err
	}
	if !resp.Success {
		return nil, nil, fmt.Errorf("lattice collapse failed: %s", resp.Error)
	}
	return resp.CollapsedWeights, resp.Probabilities, nil
}

// ============================================================================
// Phase 2: Entanglement-Based Optimization
// ============================================================================

// ApplyControlledZ applies Controlled-Z gate to create parameter correlations.
func (c *Client) ApplyControlledZ(ctx context.Context, tableauID int64, control, target, power int32) error {
	resp, err := c.client.ApplyControlledZ(ctx, &qutrittrainingrpc.ApplyControlledZRequest{
		TableauId: tableauID,
		Control:   control,
		Target:    target,
		Power:     power,
	})
	if err != nil {
		return err
	}
	if !resp.Success {
		return fmt.Errorf("apply Controlled-Z failed: %s", resp.Error)
	}
	return nil
}

// PushNoisePhase pushes noise phase for error tracking.
func (c *Client) PushNoisePhase(ctx context.Context, tableauID int64, phase int32) error {
	resp, err := c.client.PushNoisePhase(ctx, &qutrittrainingrpc.PushNoisePhaseRequest{
		TableauId: tableauID,
		Phase:     phase,
	})
	if err != nil {
		return err
	}
	if !resp.Success {
		return fmt.Errorf("push noise phase failed: %s", resp.Error)
	}
	return nil
}

// ExtractErrorSyndrome extracts error syndrome for noise handling.
func (c *Client) ExtractErrorSyndrome(ctx context.Context, tableauID int64) ([]int32, bool, error) {
	resp, err := c.client.ExtractErrorSyndrome(ctx, &qutrittrainingrpc.ExtractErrorSyndromeRequest{
		TableauId: tableauID,
	})
	if err != nil {
		return nil, false, err
	}
	if !resp.Success {
		return nil, false, fmt.Errorf("extract error syndrome failed: %s", resp.Error)
	}
	return resp.Syndrome, resp.HasError, nil
}

// ============================================================================
// Phase 3: Stabilizer Tableau Engine
// ============================================================================

// UpdatePhaseTableau updates phase tableau with discrete algebraic phase updates.
func (c *Client) UpdatePhaseTableau(ctx context.Context, tableauID int64, phaseUpdates []int32) error {
	resp, err := c.client.UpdatePhaseTableau(ctx, &qutrittrainingrpc.UpdatePhaseTableauRequest{
		TableauId:     tableauID,
		PhaseUpdates:  phaseUpdates,
	})
	if err != nil {
		return err
	}
	if !resp.Success {
		return fmt.Errorf("update phase tableau failed: %s", resp.Error)
	}
	return nil
}

// GetTrainingMetrics gets training metrics for monitoring.
func (c *Client) GetTrainingMetrics(ctx context.Context, tableauID int64) (*qutrittrainingrpc.GetTrainingMetricsResponse, error) {
	resp, err := c.client.GetTrainingMetrics(ctx, &qutrittrainingrpc.GetTrainingMetricsRequest{
		TableauId: tableauID,
	})
	if err != nil {
		return nil, err
	}
	if !resp.Success {
		return nil, fmt.Errorf("get training metrics failed: %s", resp.Error)
	}
	return resp, nil
}

// GetTableauState gets tableau state for inspection.
func (c *Client) GetTableauState(ctx context.Context, tableauID int64) ([]int32, []int32, []int32, error) {
	resp, err := c.client.GetTableauState(ctx, &qutrittrainingrpc.GetTableauStateRequest{
		TableauId: tableauID,
	})
	if err != nil {
		return nil, nil, nil, err
	}
	if !resp.Success {
		return nil, nil, nil, fmt.Errorf("get tableau state failed: %s", resp.Error)
	}
	return resp.XComponents, resp.ZComponents, resp.Phases, nil
}

// DefaultQutritTrainingAddress returns the default address for the qutrit training service.
func DefaultQutritTrainingAddress() string {
	return "localhost:50053"
}

// NewClientWithTimeout creates a new client with a context timeout.
func NewClientWithTimeout(address string, timeout time.Duration) (*Client, context.Context, context.CancelFunc, error) {
	client, err := NewClient(address)
	if err != nil {
		return nil, nil, nil, err
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	return client, ctx, cancel, nil
}