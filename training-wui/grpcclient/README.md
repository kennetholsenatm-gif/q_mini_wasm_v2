# gRPC Client Package

A robust gRPC client package for qminiwasm-core with enhanced error handling, retry logic, and metrics collection.

## Features

- **Connection Management**: Automatic reconnection, keepalive, and connection pooling
- **Retry Logic**: Configurable exponential backoff with jitter
- **Error Handling**: Comprehensive error classification and handling
- **Metrics Collection**: Request latency, success rates, and error tracking
- **Health Checking**: Connection health monitoring

## Usage

### Basic Client

```go
import "github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/grpcclient"

// Create a client with default configuration
client, err := grpcclient.NewClientWithAddress("localhost:50051")
if err != nil {
    log.Fatal(err)
}
defer client.Close()

// Check connection
if client.IsConnected() {
    fmt.Println("Connected to server")
}
```

### Custom Configuration

```go
config := grpcclient.Config{
    Address:          "localhost:50051",
    MaxRetries:       5,
    RetryBaseDelay:   200 * time.Millisecond,
    ConnectTimeout:   15 * time.Second,
    KeepaliveTime:    60 * time.Second,
    DisableTLS:       true,
}

client, err := grpcclient.NewClient(config)
if err != nil {
    log.Fatal(err)
}
defer client.Close()
```

### Retry Logic

```go
retryConfig := grpcclient.DefaultRetryConfig()
retryConfig.MaxRetries = 5

err := grpcclient.WithRetry(ctx, retryConfig, func(ctx context.Context) error {
    // Your gRPC call here
    return nil
})
```

### Error Handling

```go
err := someGRPCOperation()

if grpcclient.IsConnectionError(err) {
    // Handle connection error
    client.Reconnect()
} else if grpcclient.IsTimeoutError(err) {
    // Handle timeout
} else if grpcclient.IsNotFoundError(err) {
    // Handle not found
}
```

### Metrics Collection

```go
collector := grpcclient.NewMetricsCollector(client)

// Record a request
start := time.Now()
err := someOperation()
success := err == nil
collector.RecordRequest(start, success, false)

// Get metrics
metrics := collector.GetMetricsSnapshot()
fmt.Printf("Success rate: %.2f%%\n", metrics.SuccessRate())
fmt.Printf("Average latency: %v\n", metrics.AverageLatency())
```

## Components

### Client (`client.go`)
Core gRPC client with connection management and reconnection logic.

### Retry (`retry.go`)
Retry logic with exponential backoff and jitter.

### Errors (`errors.go`)
Comprehensive error handling and classification.

### Metrics (`metrics.go`)
Request and connection metrics collection.

## Configuration

See `Config` struct in `client.go` for all available options:

| Option | Default | Description |
|--------|---------|-------------|
| `MaxRetries` | 3 | Maximum retry attempts |
| `RetryBaseDelay` | 100ms | Base delay for exponential backoff |
| `RetryMaxDelay` | 30s | Maximum delay between retries |
| `ConnectTimeout` | 10s | Connection timeout |
| `KeepaliveTime` | 30s | Keepalive ping interval |
| `DisableTLS` | true | Disable TLS (for development) |

## Error Types

- `ErrConnectionFailed` - Connection failure
- `ErrTimeout` - Request timeout
- `ErrUnavailable` - Service unavailable
- `ErrInvalidArgument` - Invalid arguments
- `ErrNotFound` - Resource not found
- `ErrAlreadyExists` - Resource already exists
- `ErrPermissionDenied` - Permission denied
- `ErrUnauthenticated` - Authentication required
- `ErrInternal` - Internal server error
- `ErrRetriesExceeded` - All retry attempts failed