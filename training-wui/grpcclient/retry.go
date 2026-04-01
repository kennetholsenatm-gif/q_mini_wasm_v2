package grpcclient

import (
	"context"
	"math"
	"math/rand"
	"time"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// RetryConfig holds retry configuration.
type RetryConfig struct {
	MaxRetries  int
	BaseDelay   time.Duration
	MaxDelay    time.Duration
	Multiplier  float64
	Jitter      bool
	RetryableCodes []codes.Code
}

// DefaultRetryConfig returns a RetryConfig with sensible defaults.
func DefaultRetryConfig() RetryConfig {
	return RetryConfig{
		MaxRetries:  3,
		BaseDelay:   100 * time.Millisecond,
		MaxDelay:    30 * time.Second,
		Multiplier:  2.0,
		Jitter:      true,
		RetryableCodes: []codes.Code{
			codes.Unavailable,
			codes.DeadlineExceeded,
			codes.ResourceExhausted,
			codes.Aborted,
			codes.Internal,
		},
	}
}

// IsRetryable checks if an error is retryable.
func IsRetryable(err error, retryableCodes []codes.Code) bool {
	if err == nil {
		return false
	}

	st, ok := status.FromError(err)
	if !ok {
		return false
	}

	for _, code := range retryableCodes {
		if st.Code() == code {
			return true
		}
	}
	return false
}

// CalculateDelay calculates the delay for a retry attempt with exponential backoff.
func CalculateDelay(attempt int, config RetryConfig) time.Duration {
	if attempt <= 0 {
		return config.BaseDelay
	}

	delay := float64(config.BaseDelay) * math.Pow(config.Multiplier, float64(attempt))
	if delay > float64(config.MaxDelay) {
		delay = float64(config.MaxDelay)
	}

	if config.Jitter {
		// Add jitter: delay * (0.5 + random(0, 1))
		jitter := 0.5 + rand.Float64()
		delay = delay * jitter
	}

	return time.Duration(delay)
}

// RetryFunc is a function that can be retried.
type RetryFunc func(ctx context.Context) error

// WithRetry wraps a function with retry logic.
func WithRetry(ctx context.Context, config RetryConfig, fn RetryFunc) error {
	var lastErr error

	for attempt := 0; attempt <= config.MaxRetries; attempt++ {
		err := fn(ctx)
		if err == nil {
			return nil
		}

		lastErr = err

		// Check if error is retryable
		if !IsRetryable(err, config.RetryableCodes) {
			return err
		}

		// Don't wait after the last attempt
		if attempt == config.MaxRetries {
			break
		}

		// Calculate delay and wait
		delay := CalculateDelay(attempt, config)
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(delay):
			// Continue to next attempt
		}
	}

	return lastErr
}

// RetryableClient wraps a Client with retry logic.
type RetryableClient struct {
	client      *Client
	retryConfig RetryConfig
}

// NewRetryableClient creates a new retryable client.
func NewRetryableClient(config Config, retryConfig RetryConfig) (*RetryableClient, error) {
	client, err := NewClient(config)
	if err != nil {
		return nil, err
	}

	return &RetryableClient{
		client:      client,
		retryConfig: retryConfig,
	}, nil
}

// Conn returns the underlying gRPC connection.
func (rc *RetryableClient) Conn() *grpc.ClientConn {
	return rc.client.Conn()
}

// IsConnected checks if the connection is ready.
func (rc *RetryableClient) IsConnected() bool {
	return rc.client.IsConnected()
}

// Reconnect attempts to re-establish the connection.
func (rc *RetryableClient) Reconnect() error {
	return rc.client.Reconnect()
}

// Close closes the gRPC connection.
func (rc *RetryableClient) Close() error {
	return rc.client.Close()
}

// WaitForReady waits for the connection to be ready.
func (rc *RetryableClient) WaitForReady(ctx context.Context) error {
	return rc.client.WaitForReady(ctx)
}

// Address returns the server address.
func (rc *RetryableClient) Address() string {
	return rc.client.Address()
}

// Config returns the client configuration.
func (rc *RetryableClient) Config() Config {
	return rc.client.Config()
}

// RetryConfig returns the retry configuration.
func (rc *RetryableClient) RetryConfig() RetryConfig {
	return rc.retryConfig
}