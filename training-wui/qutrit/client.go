package qutrit

import (
	"context"
	"fmt"
	"time"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/grpcclient"
	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/qutritrpc"
)

// Client wraps the qutrit Clifford gRPC service client with enhanced error handling.
type Client struct {
	grpcClient *grpcclient.Client
	client     qutritrpc.QutritCliffordServiceClient
}

// NewClient creates a new qutrit Clifford gRPC client.
func NewClient(address string) (*Client, error) {
	grpcClient, err := grpcclient.NewClientWithAddress(address)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to %s: %w", address, err)
	}
	return &Client{
		grpcClient: grpcClient,
		client:     qutritrpc.NewQutritCliffordServiceClient(grpcClient.Conn()),
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
// Qutrit Tableau Operations
// ============================================================================

// CreateTableau creates a new qutrit stabilizer tableau.
func (c *Client) CreateTableau(ctx context.Context, nQutrits int32) (int64, error) {
	resp, err := c.client.CreateTableau(ctx, &qutritrpc.CreateTableauRequest{
		NQutrits: nQutrits,
	})
	if err != nil {
		return -1, err
	}
	if !resp.Success {
		return -1, fmt.Errorf("create tableau failed: %s", resp.Error)
	}
	return resp.TableauId, nil
}

// DestroyTableau destroys a qutrit tableau.
func (c *Client) DestroyTableau(ctx context.Context, tableauID int64) error {
	resp, err := c.client.DestroyTableau(ctx, &qutritrpc.DestroyTableauRequest{
		TableauId: tableauID,
	})
	if err != nil {
		return err
	}
	if !resp.Success {
		return fmt.Errorf("destroy tableau failed")
	}
	return nil
}

// ApplyH3 applies the qutrit Hadamard gate.
func (c *Client) ApplyH3(ctx context.Context, tableauID int64, qubit int32) error {
	resp, err := c.client.ApplyH3(ctx, &qutritrpc.ApplyGateRequest{
		TableauId: tableauID,
		Qubit:     qubit,
	})
	if err != nil {
		return err
	}
	if !resp.Success {
		return fmt.Errorf("apply H3 failed: %s", resp.Error)
	}
	return nil
}

// ApplyS3 applies the qutrit Phase gate.
func (c *Client) ApplyS3(ctx context.Context, tableauID int64, qubit int32) error {
	resp, err := c.client.ApplyS3(ctx, &qutritrpc.ApplyGateRequest{
		TableauId: tableauID,
		Qubit:     qubit,
	})
	if err != nil {
		return err
	}
	if !resp.Success {
		return fmt.Errorf("apply S3 failed: %s", resp.Error)
	}
	return nil
}

// ApplyCZ3 applies the qutrit Controlled-Z gate.
func (c *Client) ApplyCZ3(ctx context.Context, tableauID int64, control, target int32) error {
	resp, err := c.client.ApplyCZ3(ctx, &qutritrpc.ApplyCZ3Request{
		TableauId: tableauID,
		Control:   control,
		Target:    target,
	})
	if err != nil {
		return err
	}
	if !resp.Success {
		return fmt.Errorf("apply CZ3 failed: %s", resp.Error)
	}
	return nil
}

// MeasureTableau measures a qutrit in the computational basis.
func (c *Client) MeasureTableau(ctx context.Context, tableauID int64, qubit int32) (int32, error) {
	resp, err := c.client.MeasureTableau(ctx, &qutritrpc.MeasureRequest{
		TableauId: tableauID,
		Qubit:     qubit,
	})
	if err != nil {
		return -2, err
	}
	if !resp.Success {
		return -2, fmt.Errorf("measure failed: %s", resp.Error)
	}
	return resp.Result, nil
}

// ============================================================================
// Error Correction Operations
// ============================================================================

// Encode513 encodes a logical qutrit using the [[5,1,3]] code.
func (c *Client) Encode513(ctx context.Context, state int32) (int64, error) {
	resp, err := c.client.Encode513(ctx, &qutritrpc.EncodeRequest{
		State: state,
	})
	if err != nil {
		return -1, err
	}
	if !resp.Success {
		return -1, fmt.Errorf("encode 513 failed: %s", resp.Error)
	}
	return resp.TableauId, nil
}

// Encode312 encodes a logical qutrit using the [[3,1,2]] code.
func (c *Client) Encode312(ctx context.Context, state int32) (int64, error) {
	resp, err := c.client.Encode312(ctx, &qutritrpc.EncodeRequest{
		State: state,
	})
	if err != nil {
		return -1, err
	}
	if !resp.Success {
		return -1, fmt.Errorf("encode 312 failed: %s", resp.Error)
	}
	return resp.TableauId, nil
}

// ExtractSyndrome513 extracts the error syndrome from a [[5,1,3]] encoded tableau.
func (c *Client) ExtractSyndrome513(ctx context.Context, tableauID int64) ([]int32, error) {
	resp, err := c.client.ExtractSyndrome513(ctx, &qutritrpc.ExtractSyndromeRequest{
		TableauId: tableauID,
	})
	if err != nil {
		return nil, err
	}
	if !resp.Success {
		return nil, fmt.Errorf("extract syndrome failed: %s", resp.Error)
	}
	return resp.Syndrome, nil
}

// CorrectError513 corrects an error in a [[5,1,3]] encoded tableau.
func (c *Client) CorrectError513(ctx context.Context, tableauID int64, qutrit, errorType int32) error {
	resp, err := c.client.CorrectError513(ctx, &qutritrpc.CorrectErrorRequest{
		TableauId:  tableauID,
		Qutrit:     qutrit,
		ErrorType:  errorType,
	})
	if err != nil {
		return err
	}
	if !resp.Success {
		return fmt.Errorf("correct error failed: %s", resp.Error)
	}
	return nil
}

// ============================================================================
// Graph State Operations
// ============================================================================

// GraphStateProjection projects ternary features using a qutrit graph state.
func (c *Client) GraphStateProjection(ctx context.Context, inputFeatures []int32, graphType string, nOutput int32) ([]int32, error) {
	resp, err := c.client.GraphStateProjection(ctx, &qutritrpc.GraphStateProjectionRequest{
		InputFeatures: inputFeatures,
		GraphType:     graphType,
		NOutput:       nOutput,
	})
	if err != nil {
		return nil, err
	}
	if !resp.Success {
		return nil, fmt.Errorf("graph state projection failed: %s", resp.Error)
	}
	return resp.OutputFeatures, nil
}

// ============================================================================
// Scrambling Operations
// ============================================================================

// ScrambleWeights scrambles ternary weights for secure deployment.
func (c *Client) ScrambleWeights(ctx context.Context, weights []int32, seed uint64, depth int32) (int64, error) {
	resp, err := c.client.ScrambleWeights(ctx, &qutritrpc.ScrambleWeightsRequest{
		Weights: weights,
		Seed:    seed,
		Depth:   depth,
	})
	if err != nil {
		return -1, err
	}
	if !resp.Success {
		return -1, fmt.Errorf("scramble weights failed: %s", resp.Error)
	}
	return resp.TableauId, nil
}

// ScrambleAndDecrypt scrambles data and immediately decrypts (for testing).
func (c *Client) ScrambleAndDecrypt(ctx context.Context, data []int32, seed uint64, depth int32) ([]int32, error) {
	resp, err := c.client.ScrambleAndDecrypt(ctx, &qutritrpc.ScrambleAndDecryptRequest{
		Data:  data,
		Seed:  seed,
		Depth: depth,
	})
	if err != nil {
		return nil, err
	}
	if !resp.Success {
		return nil, fmt.Errorf("scramble and decrypt failed: %s", resp.Error)
	}
	return resp.Decrypted, nil
}

// ============================================================================
// Attention Operations
// ============================================================================

// QutritAttention computes qutrit Bell attention scores.
func (c *Client) QutritAttention(ctx context.Context, keys, queries []int32, nHeads int32) ([]float32, error) {
	resp, err := c.client.QutritAttention(ctx, &qutritrpc.QutritAttentionRequest{
		Keys:    keys,
		Queries: queries,
		NHeads:  nHeads,
	})
	if err != nil {
		return nil, err
	}
	if !resp.Success {
		return nil, fmt.Errorf("qutrit attention failed: %s", resp.Error)
	}
	return resp.Scores, nil
}

// SoftmaxApproximation approximates Softmax using discrete stabilizer probabilities.
func (c *Client) SoftmaxApproximation(ctx context.Context, scores []float32, temperature float32) ([]float32, error) {
	resp, err := c.client.SoftmaxApproximation(ctx, &qutritrpc.SoftmaxApproximationRequest{
		Scores:      scores,
		Temperature: temperature,
	})
	if err != nil {
		return nil, err
	}
	if !resp.Success {
		return nil, fmt.Errorf("softmax approximation failed: %s", resp.Error)
	}
	return resp.DiscreteScores, nil
}

// TopKAttention selects top-k attention indices.
func (c *Client) TopKAttention(ctx context.Context, scores []float32, k int32) ([]int32, error) {
	resp, err := c.client.TopKAttention(ctx, &qutritrpc.TopKAttentionRequest{
		Scores: scores,
		K:      k,
	})
	if err != nil {
		return nil, err
	}
	if !resp.Success {
		return nil, fmt.Errorf("top-k attention failed: %s", resp.Error)
	}
	return resp.Indices, nil
}

// DefaultQutritAddress returns the default address for the qutrit Clifford service.
func DefaultQutritAddress() string {
	return "localhost:50052"
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