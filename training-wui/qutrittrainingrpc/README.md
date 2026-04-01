# Qutrit Training gRPC Stubs

This directory contains Go stubs for the Qutrit Training gRPC service.

## Generation

To regenerate the stubs, run from the repository root:

```bash
protoc -I proto proto/qutrit_training.proto \
  --go_out=./training-wui/qutrittrainingrpc \
  --go_opt=paths=source_relative \
  --go-grpc_out=./training-wui/qutrittrainingrpc \
  --go-grpc_opt=paths=source_relative
```

## Requirements

- `protoc` (Protocol Buffers compiler)
- `protoc-gen-go` and `protoc-gen-go-grpc` plugins

Install with:
```bash
go install google.golang.org/protobuf/cmd/protoc-gen-go@latest
go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest
```

## Proto Source

See [`proto/qutrit_training.proto`](../../proto/qutrit_training.proto) for the service definition.

## Service Overview

The `QutritTrainingService` implements the Qutrit Clifford training paradigm:

### Phase 1: Quantum Superposition
- `InitializeSuperposition` - Create tableau with parameters in superposition
- `ApplyHadamardLayer` - Apply H₃ gates to create superposition
- `LatticeCollapse` - Projective measurement for weight finalization

### Phase 2: Entanglement-Based Optimization
- `ApplyControlledZ` - Create parameter correlations via CZ₃
- `PushNoisePhase` - Push noise for error tracking
- `ExtractErrorSyndrome` - Extract error syndrome for noise handling

### Phase 3: Stabilizer Tableau Engine
- `UpdatePhaseTableau` - Discrete algebraic phase updates
- `GetTrainingMetrics` - Monitor training progress
- `GetTableauState` - Inspect tableau state

## Usage

```go
import "github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/qutrittraining"

// Create client
client, err := qutrittraining.NewClient("localhost:50053")
if err != nil {
    log.Fatal(err)
}
defer client.Close()

// Initialize superposition
tableauID, err := client.InitializeSuperposition(ctx, 1024, 42)

// Apply Hadamard layer
err = client.ApplyHadamardLayer(ctx, tableauID, []int32{})

// Lattice collapse
weights, probs, err := client.LatticeCollapse(ctx, tableauID, 100)
```

## Research

See [`docs/research/Qutrit Clifford Training for QMINIWASM.md`](../../docs/research/Qutrit Clifford Training for QMINIWASM.md) for the theoretical foundation.