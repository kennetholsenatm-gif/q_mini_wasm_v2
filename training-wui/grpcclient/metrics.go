package grpcclient

import (
	"sync"
	"time"
)

// Metrics holds gRPC client metrics.
type Metrics struct {
	mu sync.RWMutex

	// TotalRequests is the total number of requests made.
	TotalRequests int64

	// SuccessfulRequests is the number of successful requests.
	SuccessfulRequests int64

	// FailedRequests is the number of failed requests.
	FailedRequests int64

	// RetriedRequests is the number of requests that were retried.
	RetriedRequests int64

	// TotalLatency is the cumulative latency of all requests.
	TotalLatency time.Duration

	// ConnectionCount is the number of connections made.
	ConnectionCount int64

	// ReconnectionCount is the number of reconnections.
	ReconnectionCount int64

	// LastError is the last error that occurred.
	LastError error

	// LastErrorTime is the time of the last error.
	LastErrorTime time.Time
}

// NewMetrics creates a new Metrics instance.
func NewMetrics() *Metrics {
	return &Metrics{}
}

// RecordRequest records a request.
func (m *Metrics) RecordRequest(latency time.Duration, success bool, retried bool) {
	m.mu.Lock()
	defer m.mu.Unlock()

	m.TotalRequests++
	m.TotalLatency += latency

	if success {
		m.SuccessfulRequests++
	} else {
		m.FailedRequests++
	}

	if retried {
		m.RetriedRequests++
	}
}

// RecordConnection records a connection.
func (m *Metrics) RecordConnection() {
	m.mu.Lock()
	defer m.mu.Unlock()

	m.ConnectionCount++
}

// RecordReconnection records a reconnection.
func (m *Metrics) RecordReconnection() {
	m.mu.Lock()
	defer m.mu.Unlock()

	m.ReconnectionCount++
}

// RecordError records an error.
func (m *Metrics) RecordError(err error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	m.LastError = err
	m.LastErrorTime = time.Now()
}

// GetMetrics returns a copy of the current metrics.
func (m *Metrics) GetMetrics() MetricsSnapshot {
	m.mu.RLock()
	defer m.mu.RUnlock()

	return MetricsSnapshot{
		TotalRequests:      m.TotalRequests,
		SuccessfulRequests: m.SuccessfulRequests,
		FailedRequests:     m.FailedRequests,
		RetriedRequests:    m.RetriedRequests,
		TotalLatency:       m.TotalLatency,
		ConnectionCount:    m.ConnectionCount,
		ReconnectionCount:  m.ReconnectionCount,
		LastError:          m.LastError,
		LastErrorTime:      m.LastErrorTime,
	}
}

// Reset resets all metrics.
func (m *Metrics) Reset() {
	m.mu.Lock()
	defer m.mu.Unlock()

	m.TotalRequests = 0
	m.SuccessfulRequests = 0
	m.FailedRequests = 0
	m.RetriedRequests = 0
	m.TotalLatency = 0
	m.ConnectionCount = 0
	m.ReconnectionCount = 0
	m.LastError = nil
	m.LastErrorTime = time.Time{}
}

// MetricsSnapshot is a snapshot of metrics.
type MetricsSnapshot struct {
	TotalRequests      int64
	SuccessfulRequests int64
	FailedRequests     int64
	RetriedRequests    int64
	TotalLatency       time.Duration
	ConnectionCount    int64
	ReconnectionCount  int64
	LastError          error
	LastErrorTime      time.Time
}

// SuccessRate returns the success rate as a percentage.
func (ms MetricsSnapshot) SuccessRate() float64 {
	if ms.TotalRequests == 0 {
		return 0
	}
	return float64(ms.SuccessfulRequests) / float64(ms.TotalRequests) * 100
}

// AverageLatency returns the average latency.
func (ms MetricsSnapshot) AverageLatency() time.Duration {
	if ms.TotalRequests == 0 {
		return 0
	}
	return ms.TotalLatency / time.Duration(ms.TotalRequests)
}

// MetricsCollector wraps a Client with metrics collection.
type MetricsCollector struct {
	client  *Client
	metrics *Metrics
}

// NewMetricsCollector creates a new metrics collector.
func NewMetricsCollector(client *Client) *MetricsCollector {
	return &MetricsCollector{
		client:  client,
		metrics: NewMetrics(),
	}
}

// Client returns the underlying client.
func (mc *MetricsCollector) Client() *Client {
	return mc.client
}

// Metrics returns the metrics.
func (mc *MetricsCollector) Metrics() *Metrics {
	return mc.metrics
}

// GetMetricsSnapshot returns a snapshot of the metrics.
func (mc *MetricsCollector) GetMetricsSnapshot() MetricsSnapshot {
	return mc.metrics.GetMetrics()
}

// ResetMetrics resets all metrics.
func (mc *MetricsCollector) ResetMetrics() {
	mc.metrics.Reset()
}

// RecordRequest records a request with metrics.
func (mc *MetricsCollector) RecordRequest(start time.Time, success bool, retried bool) {
	latency := time.Since(start)
	mc.metrics.RecordRequest(latency, success, retried)
}

// RecordConnection records a connection with metrics.
func (mc *MetricsCollector) RecordConnection() {
	mc.metrics.RecordConnection()
}

// RecordReconnection records a reconnection with metrics.
func (mc *MetricsCollector) RecordReconnection() {
	mc.metrics.RecordReconnection()
}

// RecordError records an error with metrics.
func (mc *MetricsCollector) RecordError(err error) {
	mc.metrics.RecordError(err)
}