package qutrittraining

import (
	"context"
	"testing"
	"time"
)

func TestNewClient(t *testing.T) {
	client, err := NewClient("localhost:50053")
	if err != nil {
		t.Fatalf("NewClient failed: %v", err)
	}
	defer client.Close()

	if client == nil {
		t.Fatal("NewClient returned nil")
	}
}

func TestNewClientWithTimeout(t *testing.T) {
	client, ctx, cancel, err := NewClientWithTimeout("localhost:50053", 5*time.Second)
	if err != nil {
		t.Fatalf("NewClientWithTimeout failed: %v", err)
	}
	defer cancel()
	defer client.Close()

	if client == nil {
		t.Fatal("NewClientWithTimeout returned nil")
	}
	if ctx == nil {
		t.Fatal("NewClientWithTimeout returned nil context")
	}
}

func TestClientClose(t *testing.T) {
	client, err := NewClient("localhost:50053")
	if err != nil {
		t.Fatalf("NewClient failed: %v", err)
	}

	err = client.Close()
	if err != nil {
		t.Fatalf("Close failed: %v", err)
	}
}

func TestClientIsConnected(t *testing.T) {
	client, err := NewClient("localhost:50053")
	if err != nil {
		t.Fatalf("NewClient failed: %v", err)
	}
	defer client.Close()

	// Connection status depends on whether the service is running
	connected := client.IsConnected()
	t.Logf("IsConnected: %v", connected)
}

func TestClientReconnect(t *testing.T) {
	client, err := NewClient("localhost:50053")
	if err != nil {
		t.Fatalf("NewClient failed: %v", err)
	}
	defer client.Close()

	err = client.Reconnect()
	if err != nil {
		t.Logf("Reconnect failed (expected if service not running): %v", err)
	}
}

func TestDefaultQutritTrainingAddress(t *testing.T) {
	addr := DefaultQutritTrainingAddress()
	if addr == "" {
		t.Fatal("DefaultQutritTrainingAddress returned empty string")
	}
	t.Logf("Default address: %s", addr)
}

// The following tests require a running qutrit training service

func TestInitializeSuperposition(t *testing.T) {
	client, ctx, cancel, err := NewClientWithTimeout("localhost:50053", 10*time.Second)
	if err != nil {
		t.Skipf("Cannot connect to service: %v", err)
		return
	}
	defer cancel()
	defer client.Close()

	tableauID, err := client.InitializeSuperposition(ctx, 5, 42)
	if err != nil {
		t.Fatalf("InitializeSuperposition failed: %v", err)
	}
	if tableauID < 0 {
		t.Fatalf("Invalid tableau ID: %d", tableauID)
	}
	t.Logf("Created tableau: %d", tableauID)
}

func TestApplyHadamardLayer(t *testing.T) {
	client, ctx, cancel, err := NewClientWithTimeout("localhost:50053", 10*time.Second)
	if err != nil {
		t.Skipf("Cannot connect to service: %v", err)
		return
	}
	defer cancel()
	defer client.Close()

	tableauID, err := client.InitializeSuperposition(ctx, 3, 42)
	if err != nil {
		t.Fatalf("InitializeSuperposition failed: %v", err)
	}

	// Apply to all qubits
	err = client.ApplyHadamardLayer(ctx, tableauID, []int32{})
	if err != nil {
		t.Fatalf("ApplyHadamardLayer failed: %v", err)
	}

	// Apply to specific qubits
	err = client.ApplyHadamardLayer(ctx, tableauID, []int32{0, 2})
	if err != nil {
		t.Fatalf("ApplyHadamardLayer with specific qubits failed: %v", err)
	}
}

func TestLatticeCollapse(t *testing.T) {
	client, ctx, cancel, err := NewClientWithTimeout("localhost:50053", 10*time.Second)
	if err != nil {
		t.Skipf("Cannot connect to service: %v", err)
		return
	}
	defer cancel()
	defer client.Close()

	tableauID, err := client.InitializeSuperposition(ctx, 3, 42)
	if err != nil {
		t.Fatalf("InitializeSuperposition failed: %v", err)
	}

	err = client.ApplyHadamardLayer(ctx, tableauID, []int32{})
	if err != nil {
		t.Fatalf("ApplyHadamardLayer failed: %v", err)
	}

	weights, probs, err := client.LatticeCollapse(ctx, tableauID, 100)
	if err != nil {
		t.Fatalf("LatticeCollapse failed: %v", err)
	}

	if len(weights) != 3 {
		t.Fatalf("Expected 3 weights, got %d", len(weights))
	}
	if len(probs) != 3 {
		t.Fatalf("Expected 3 probabilities, got %d", len(probs))
	}

	for i, w := range weights {
		if w < -1 || w > 1 {
			t.Errorf("Weight %d out of range [-1, 0, 1]: %d", i, w)
		}
	}

	for i, p := range probs {
		if p < 0 || p > 1 {
			t.Errorf("Probability %d out of range [0, 1]: %f", i, p)
		}
	}

	t.Logf("Weights: %v, Probabilities: %v", weights, probs)
}

func TestApplyControlledZ(t *testing.T) {
	client, ctx, cancel, err := NewClientWithTimeout("localhost:50053", 10*time.Second)
	if err != nil {
		t.Skipf("Cannot connect to service: %v", err)
		return
	}
	defer cancel()
	defer client.Close()

	tableauID, err := client.InitializeSuperposition(ctx, 3, 42)
	if err != nil {
		t.Fatalf("InitializeSuperposition failed: %v", err)
	}

	err = client.ApplyControlledZ(ctx, tableauID, 0, 1, 1)
	if err != nil {
		t.Fatalf("ApplyControlledZ failed: %v", err)
	}
}

func TestGetTrainingMetrics(t *testing.T) {
	client, ctx, cancel, err := NewClientWithTimeout("localhost:50053", 10*time.Second)
	if err != nil {
		t.Skipf("Cannot connect to service: %v", err)
		return
	}
	defer cancel()
	defer client.Close()

	tableauID, err := client.InitializeSuperposition(ctx, 5, 42)
	if err != nil {
		t.Fatalf("InitializeSuperposition failed: %v", err)
	}

	metrics, err := client.GetTrainingMetrics(ctx, tableauID)
	if err != nil {
		t.Fatalf("GetTrainingMetrics failed: %v", err)
	}

	if metrics.TotalEpochs != 0 {
		t.Errorf("Expected 0 total epochs, got %d", metrics.TotalEpochs)
	}
	if metrics.SuccessfulEpochs != 0 {
		t.Errorf("Expected 0 successful epochs, got %d", metrics.SuccessfulEpochs)
	}

	t.Logf("Metrics: total_epochs=%d, successful=%d, loss=%.4f, best_loss=%.4f",
		metrics.TotalEpochs, metrics.SuccessfulEpochs, metrics.CurrentLoss, metrics.BestLoss)
}

func TestGetTableauState(t *testing.T) {
	client, ctx, cancel, err := NewClientWithTimeout("localhost:50053", 10*time.Second)
	if err != nil {
		t.Skipf("Cannot connect to service: %v", err)
		return
	}
	defer cancel()
	defer client.Close()

	tableauID, err := client.InitializeSuperposition(ctx, 3, 42)
	if err != nil {
		t.Fatalf("InitializeSuperposition failed: %v", err)
	}

	xComp, zComp, phases, err := client.GetTableauState(ctx, tableauID)
	if err != nil {
		t.Fatalf("GetTableauState failed: %v", err)
	}

	// 3 qutrits = 3x3 matrix = 9 components
	if len(xComp) != 9 {
		t.Errorf("Expected 9 X components, got %d", len(xComp))
	}
	if len(zComp) != 9 {
		t.Errorf("Expected 9 Z components, got %d", len(zComp))
	}
	if len(phases) != 3 {
		t.Errorf("Expected 3 phases, got %d", len(phases))
	}

	t.Logf("X components: %v", xComp)
	t.Logf("Z components: %v", zComp)
	t.Logf("Phases: %v", phases)
}

func TestUpdatePhaseTableau(t *testing.T) {
	client, ctx, cancel, err := NewClientWithTimeout("localhost:50053", 10*time.Second)
	if err != nil {
		t.Skipf("Cannot connect to service: %v", err)
		return
	}
	defer cancel()
	defer client.Close()

	tableauID, err := client.InitializeSuperposition(ctx, 3, 42)
	if err != nil {
		t.Fatalf("InitializeSuperposition failed: %v", err)
	}

	updates := []int32{0, 1, 2}
	err = client.UpdatePhaseTableau(ctx, tableauID, updates)
	if err != nil {
		t.Fatalf("UpdatePhaseTableau failed: %v", err)
	}

	// Verify metrics were updated
	metrics, err := client.GetTrainingMetrics(ctx, tableauID)
	if err != nil {
		t.Fatalf("GetTrainingMetrics failed: %v", err)
	}

	if metrics.TotalEpochs != 1 {
		t.Errorf("Expected 1 total epoch, got %d", metrics.TotalEpochs)
	}
	if metrics.SuccessfulEpochs != 1 {
		t.Errorf("Expected 1 successful epoch, got %d", metrics.SuccessfulEpochs)
	}
}