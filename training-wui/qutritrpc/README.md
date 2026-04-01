# Qutrit Clifford gRPC Stubs

This directory contains Go stubs for the Qutrit Clifford gRPC service.

## Generation

To regenerate the stubs, run from the repository root:

```bash
protoc -I proto proto/qutrit_clifford.proto \
  --go_out=./training-wui/qutritrpc \
  --go_opt=paths=source_relative \
  --go-grpc_out=./training-wui/qutritrpc \
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

See [`proto/qutrit_clifford.proto`](../../proto/qutrit_clifford.proto) for the service definition.

## Service Overview

The `QutritCliffordService` provides gRPC access to:

1. **Qutrit Tableau Operations** - Create, manipulate, and measure qutrit stabilizer tableaus
2. **Error Correction** - [[5,1,3]] and [[3,1,2]] qutrit stabilizer codes
3. **Graph States** - Deterministic feature projection via qutrit graph states
4. **Scrambling** - Clifford-based Zero-Trust Execution Environment (ZTEE)
5. **Attention** - Bell difference sampling for Softmax approximation