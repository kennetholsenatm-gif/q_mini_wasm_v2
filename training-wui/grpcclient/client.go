package grpcclient

import (
	"context"
	"fmt"
	"sync"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/connectivity"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/keepalive"
)

// Config holds gRPC client configuration.
type Config struct {
	// Address is the server address (e.g., "localhost:50051")
	Address string

	// MaxRecvMsgSize is the maximum message size in bytes (default: 4MB)
	MaxRecvMsgSize int

	// MaxSendMsgSize is the maximum send message size in bytes (default: 4MB)
	MaxSendMsgSize int

	// KeepaliveTime is the keepalive ping interval (default: 30s)
	KeepaliveTime time.Duration

	// KeepaliveTimeout is the keepalive ping timeout (default: 10s)
	KeepaliveTimeout time.Duration

	// InitialWindowSize is the initial window size for stream flow control (default: 65535)
	InitialWindowSize int32

	// InitialConnWindowSize is the initial window size for connection flow control (default: 1048576)
	InitialConnWindowSize int32

	// MaxRetries is the maximum number of retry attempts (default: 3)
	MaxRetries int

	// RetryBaseDelay is the base delay for exponential backoff (default: 100ms)
	RetryBaseDelay time.Duration

	// RetryMaxDelay is the maximum delay between retries (default: 30s)
	RetryMaxDelay time.Duration

	// ConnectTimeout is the connection timeout (default: 10s)
	ConnectTimeout time.Duration

	// EnableHealthCheck enables health checking (default: true)
	EnableHealthCheck bool

	// DisableTLS disables TLS (default: true for development)
	DisableTLS bool
}

// DefaultConfig returns a Config with sensible defaults.
func DefaultConfig(address string) Config {
	return Config{
		Address:               address,
		MaxRecvMsgSize:        4 * 1024 * 1024, // 4MB
		MaxSendMsgSize:        4 * 1024 * 1024, // 4MB
		KeepaliveTime:         30 * time.Second,
		KeepaliveTimeout:      10 * time.Second,
		InitialWindowSize:     65535,
		InitialConnWindowSize: 1048576,
		MaxRetries:            3,
		RetryBaseDelay:        100 * time.Millisecond,
		RetryMaxDelay:         30 * time.Second,
		ConnectTimeout:        10 * time.Second,
		EnableHealthCheck:     true,
		DisableTLS:            true,
	}
}

// Client wraps a gRPC connection with enhanced error handling and reconnection.
type Client struct {
	config Config
	conn   *grpc.ClientConn
	mu     sync.RWMutex
}

// NewClient creates a new gRPC client with the given configuration.
func NewClient(config Config) (*Client, error) {
	client := &Client{
		config: config,
	}

	if err := client.connect(); err != nil {
		return nil, fmt.Errorf("failed to connect: %w", err)
	}

	return client, nil
}

// NewClientWithAddress creates a new gRPC client with default configuration.
func NewClientWithAddress(address string) (*Client, error) {
	return NewClient(DefaultConfig(address))
}

// connect establishes the gRPC connection.
func (c *Client) connect() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	// Build dial options
	opts := []grpc.DialOption{
		grpc.WithDefaultCallOptions(
			grpc.MaxCallRecvMsgSize(c.config.MaxRecvMsgSize),
			grpc.MaxCallSendMsgSize(c.config.MaxSendMsgSize),
		),
		grpc.WithInitialWindowSize(c.config.InitialWindowSize),
		grpc.WithInitialConnWindowSize(c.config.InitialConnWindowSize),
		grpc.WithKeepaliveParams(keepalive.ClientParameters{
			Time:                c.config.KeepaliveTime,
			Timeout:             c.config.KeepaliveTimeout,
			PermitWithoutStream: true,
		}),
	}

	// Add TLS or insecure option
	if c.config.DisableTLS {
		opts = append(opts, grpc.WithTransportCredentials(insecure.NewCredentials()))
	}

	// Create connection with timeout
	ctx, cancel := context.WithTimeout(context.Background(), c.config.ConnectTimeout)
	defer cancel()

	conn, err := grpc.DialContext(ctx, c.config.Address, opts...)
	if err != nil {
		return fmt.Errorf("failed to dial %s: %w", c.config.Address, err)
	}

	c.conn = conn
	return nil
}

// Conn returns the underlying gRPC connection.
func (c *Client) Conn() *grpc.ClientConn {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.conn
}

// IsConnected checks if the connection is ready.
func (c *Client) IsConnected() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()

	if c.conn == nil {
		return false
	}

	state := c.conn.GetState()
	return state == connectivity.Ready || state == connectivity.Idle
}

// Reconnect attempts to re-establish the connection.
func (c *Client) Reconnect() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.conn != nil {
		c.conn.Close()
		c.conn = nil
	}

	return c.connect()
}

// Close closes the gRPC connection.
func (c *Client) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.conn != nil {
		err := c.conn.Close()
		c.conn = nil
		return err
	}
	return nil
}

// WaitForReady waits for the connection to be ready.
func (c *Client) WaitForReady(ctx context.Context) error {
	c.mu.RLock()
	conn := c.conn
	c.mu.RUnlock()

	if conn == nil {
		return fmt.Errorf("connection is nil")
	}

	for {
		state := conn.GetState()
		if state == connectivity.Ready {
			return nil
		}
		if state == connectivity.Shutdown {
			return fmt.Errorf("connection is shutdown")
		}
		if !conn.WaitForStateChange(ctx, state) {
			return ctx.Err()
		}
	}
}

// Address returns the server address.
func (c *Client) Address() string {
	return c.config.Address
}

// Config returns the client configuration.
func (c *Client) Config() Config {
	return c.config
}