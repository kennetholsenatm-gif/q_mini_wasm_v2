package pkg

import (
	"bytes"
	"encoding/json"
	"math"
	"net/http"
	"sync"
	"time"
)

// RAGChunk represents a retrieved context chunk
type RAGChunk struct {
	Content         string  `json:"content"`
	SourceFile      string  `json:"source_file"`
	StartLine       int     `json:"start_line"`
	EndLine         int     `json:"end_line"`
	Score           float64 `json:"score"`
	QuantumEntropy  float64 `json:"quantum_entropy"`
	StabilizerPhase int     `json:"stabilizer_phase"`
	LoadFactor      float64 `json:"load_factor"`
	ChunkType       string  `json:"chunk_type"`
}

// RAGResult represents result from RAG retrieval
type RAGResult struct {
	Chunks            []RAGChunk `json:"chunks"`
	TotalTokens       int        `json:"total_tokens"`
	LatencyMs         int        `json:"latency_ms"`
	QueryComplexity   float64    `json:"query_complexity"`
	EntanglementScore float64    `json:"entanglement_score"`
	LoadBalanceFactor float64    `json:"load_balance_factor"`
	RoutedChunks      int        `json:"routed_chunks"`
}

// ContextText returns concatenated context text
func (r *RAGResult) ContextText() string {
	var parts []string
	for _, chunk := range r.Chunks {
		parts = append(parts, "--- "+chunk.SourceFile+" (lines "+string(rune(chunk.StartLine))+"-"+string(rune(chunk.EndLine))+" ) ---")
		parts = append(parts, chunk.Content)
	}
	return bytes.Join(parts, []byte("\n")).String()
}

// SourceFiles returns unique source files
func (r *RAGResult) SourceFiles() []string {
	files := make(map[string]bool)
	for _, chunk := range r.Chunks {
		files[chunk.SourceFile] = true
	}

	result := make([]string, 0, len(files))
	for f := range files {
		result = append(result, f)
	}
	return result
}

// RAGClient provides integration with RAG service
type RAGClient struct {
	httpEndpoint string
	timeout      time.Duration
	cacheTTL     time.Duration
	client       *http.Client
	cache        map[string]cacheEntry
	cacheMutex   sync.Mutex
}

type cacheEntry struct {
	result    RAGResult
	timestamp time.Time
}

// NewRAGClient creates a new RAGClient
func NewRAGClient(httpEndpoint string, timeout time.Duration, cacheTTL time.Duration) *RAGClient {
	if httpEndpoint == "" {
		httpEndpoint = "http://localhost:8088"
	}
	if timeout == 0 {
		timeout = 30 * time.Second
	}
	if cacheTTL == 0 {
		cacheTTL = 300 * time.Second
	}

	return &RAGClient{
		httpEndpoint: httpEndpoint,
		timeout:      timeout,
		cacheTTL:     cacheTTL,
		client: &http.Client{
			Timeout: timeout,
		},
		cache: make(map[string]cacheEntry),
	}
}

// getCacheKey generates cache key
func (c *RAGClient) getCacheKey(query string, maxTokens int, contextType string) string {
	return query + ":" + string(rune(maxTokens)) + ":" + contextType
}

// isCacheValid checks if cache entry is valid
func (c *RAGClient) isCacheValid(timestamp time.Time) bool {
	return time.Since(timestamp) < c.cacheTTL
}

// RetrieveContext retrieves relevant context for a query
func (c *RAGClient) RetrieveContext(query string, maxTokens int, contextType string, enableQuantumScoring bool, enableLLEP bool) (*RAGResult, error) {
	// Check cache
	cacheKey := c.getCacheKey(query, maxTokens, contextType)

	c.cacheMutex.Lock()
	if entry, ok := c.cache[cacheKey]; ok && c.isCacheValid(entry.timestamp) {
		c.cacheMutex.Unlock()
		return &entry.result, nil
	}
	c.cacheMutex.Unlock()

	// Prepare request
	requestBody := map[string]interface{}{
		"query":        query,
		"max_tokens":   maxTokens,
		"context_type": contextType,
	}

	jsonBody, err := json.Marshal(requestBody)
	if err != nil {
		return &RAGResult{}, err
	}

	// Execute request
	resp, err := c.client.Post(c.httpEndpoint+"/api/v1/rag/retrieve", "application/json", bytes.NewBuffer(jsonBody))
	if err != nil {
		return &RAGResult{}, nil
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return &RAGResult{}, nil
	}

	var data map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&data); err != nil {
		return &RAGResult{}, err
	}

	// Parse chunks
	chunks := make([]RAGChunk, 0)
	if chunkData, ok := data["chunks"].([]interface{}); ok {
		for _, cd := range chunkData {
			if chunkMap, ok := cd.(map[string]interface{}); ok {
				chunk := RAGChunk{
					Content:    getString(chunkMap, "content"),
					SourceFile: getString(chunkMap, "source_file"),
					StartLine:  getInt(chunkMap, "start_line"),
					EndLine:    getInt(chunkMap, "end_line"),
					Score:      getFloat64(chunkMap, "score"),
					ChunkType:  getString(chunkMap, "chunk_type", "text"),
				}
				chunks = append(chunks, chunk)
			}
		}
	}

	// Apply Quantum Entanglement Entropy Scoring
	if enableQuantumScoring {
		chunks = c.applyQuantumEntanglementScoring(chunks)
	}

	// Apply Cognitive Ergonomics Chunking (Miller's Law 7±2)
	ergonomicGroups := c.applyCognitiveErgonomicsChunking(chunks)

	// Flatten groups
	chunks = make([]RAGChunk, 0)
	for _, group := range ergonomicGroups {
		chunks = append(chunks, group...)
	}

	// Apply LLEP Load Balancing
	routedChunks := 0
	if enableLLEP && len(chunks) > 0 {
		chunks, routedChunks = c.applyLLEPLoadBalancing(chunks)
	}

	// Calculate metrics
	totalEntanglement := 0.0
	totalLoad := 0.0
	for _, chunk := range chunks {
		totalEntanglement += chunk.QuantumEntropy
		totalLoad += chunk.LoadFactor
	}

	avgLoad := totalLoad / math.Max(1.0, float64(len(chunks)))

	result := &RAGResult{
		Chunks:            chunks,
		TotalTokens:       getInt(data, "total_tokens"),
		LatencyMs:         getInt(data, "latency_ms"),
		QueryComplexity:   getFloat64(data, "query_complexity"),
		EntanglementScore: totalEntanglement,
		LoadBalanceFactor: avgLoad,
		RoutedChunks:      routedChunks,
	}

	// Cache result
	c.cacheMutex.Lock()
	c.cache[cacheKey] = cacheEntry{
		result:    *result,
		timestamp: time.Now(),
	}
	c.cacheMutex.Unlock()

	return result, nil
}

// GetMetrics gets RAG service metrics
func (c *RAGClient) GetMetrics() (map[string]interface{}, error) {
	resp, err := c.client.Get(c.httpEndpoint + "/api/v1/rag/metrics")
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	var metrics map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&metrics); err != nil {
		return nil, err
	}

	return metrics, nil
}

// IndexDocument indexes a document into RAG vector store
func (c *RAGClient) IndexDocument(filePath string) (bool, error) {
	requestBody := map[string]interface{}{
		"file_path": filePath,
	}

	jsonBody, err := json.Marshal(requestBody)
	if err != nil {
		return false, err
	}

	resp, err := c.client.Post(c.httpEndpoint+"/api/v1/rag/index", "application/json", bytes.NewBuffer(jsonBody))
	if err != nil {
		return false, err
	}
	defer resp.Body.Close()

	return resp.StatusCode == http.StatusOK, nil
}

// HealthCheck checks if RAG service is healthy
func (c *RAGClient) HealthCheck() bool {
	resp, err := c.client.Get(c.httpEndpoint + "/api/v1/health")
	if err != nil {
		return false
	}
	defer resp.Body.Close()

	return resp.StatusCode == http.StatusOK
}

// applyQuantumEntanglementScoring applies GF(3) qutrit entanglement entropy scoring
func (c *RAGClient) applyQuantumEntanglementScoring(chunks []RAGChunk) []RAGChunk {
	scoredChunks := make([]RAGChunk, len(chunks))

	for i, chunk := range chunks {
		signatureHash := int(hashString(chunk.Content) % 2187) // 3^7 = 2187 states

		t0 := signatureHash / 729
		remainder := signatureHash % 729
		t1 := remainder / 243
		remainder %= 243
		t2 := remainder / 81
		remainder %= 81
		t3 := remainder / 27
		remainder %= 27
		t4 := remainder / 9
		remainder %= 9
		t5 := remainder / 3

		symplecticProduct := (t0*t3 + t1*t4 + t2*t5) % 3

		var entropy float64
		if symplecticProduct == 0 {
			entropy = 1.0
		} else {
			purity := math.Pow(math.Cos(float64(symplecticProduct)*math.Pi/3.0), 2)
			if purity > 0 && purity < 1.0 {
				entropy = -purity*math.Log2(purity) - (1-purity)*math.Log2(1-purity)
			} else {
				entropy = 0.0
			}
		}

		chunk.QuantumEntropy = entropy
		chunk.Score = (chunk.Score * 0.4) + (entropy * 0.6)
		scoredChunks[i] = chunk
	}

	// Sort by score descending
	for i := range scoredChunks {
		for j := i + 1; j < len(scoredChunks); j++ {
			if scoredChunks[j].Score > scoredChunks[i].Score {
				scoredChunks[i], scoredChunks[j] = scoredChunks[j], scoredChunks[i]
			}
		}
	}

	return scoredChunks
}

// applyLLEPLoadBalancing applies Least-Loaded Expert Parallelism routing
func (c *RAGClient) applyLLEPLoadBalancing(chunks []RAGChunk) ([]RAGChunk, int) {
	const EXPERT_COUNT = 8
	maxCapacity := float64(len(chunks)) / EXPERT_COUNT
	overflowThreshold := maxCapacity * 1.2

	expertLoads := make([]float64, EXPERT_COUNT)
	routedChunks := 0
	balancedChunks := make([]RAGChunk, len(chunks))

	for i, chunk := range chunks {
		expertIndex := int(hashString(chunk.SourceFile) % EXPERT_COUNT)

		if expertLoads[expertIndex] >= overflowThreshold {
			// Find least loaded expert
			minLoad := expertLoads[0]
			targetExpert := 0
			for j := 1; j < EXPERT_COUNT; j++ {
				if expertLoads[j] < minLoad {
					minLoad = expertLoads[j]
					targetExpert = j
				}
			}

			chunk.LoadFactor = expertLoads[targetExpert] / math.Max(maxCapacity, 0.001)
			expertLoads[targetExpert] += 1.0
			routedChunks++
		} else {
			chunk.LoadFactor = expertLoads[expertIndex] / math.Max(maxCapacity, 0.001)
			expertLoads[expertIndex] += 1.0
		}

		balancedChunks[i] = chunk
	}

	return balancedChunks, routedChunks
}

// applyCognitiveErgonomicsChunking applies Miller's Law 7±2 chunking
func (c *RAGClient) applyCognitiveErgonomicsChunking(chunks []RAGChunk) [][]RAGChunk {
	const CHUNK_SIZE = 7

	if len(chunks) == 0 {
		return [][]RAGChunk{}
	}

	result := make([][]RAGChunk, 0)
	for i := 0; i < len(chunks); i += CHUNK_SIZE {
		end := i + CHUNK_SIZE
		if end > len(chunks) {
			end = len(chunks)
		}
		result = append(result, chunks[i:end])
	}

	return result
}

// GetHypersimplexGeometry calculates hypersimplex geometric metrics
func (c *RAGClient) GetHypersimplexGeometry(chunks []RAGChunk) map[string]interface{} {
	if len(chunks) == 0 {
		return map[string]interface{}{}
	}

	expertLoads := make([]int, 8)
	for _, chunk := range chunks {
		idx := int(hashString(chunk.SourceFile) % 8)
		expertLoads[idx]++
	}

	maxLoad := expertLoads[0]
	minLoad := expertLoads[0]
	sum := 0.0
	for _, load := range expertLoads {
		if load > maxLoad {
			maxLoad = load
		}
		if load < minLoad {
			minLoad = load
		}
		sum += math.Pow(float64(load)-float64(len(chunks))/8.0, 2)
	}

	loadStdDev := math.Sqrt(sum / 8.0)
	loadImbalanceRatio := float64(maxLoad) / math.Max(float64(minLoad), 1.0)

	return map[string]interface{}{
		"expert_loads":           expertLoads,
		"load_imbalance_ratio":   loadImbalanceRatio,
		"load_std_dev":           loadStdDev,
		"combinatorial_depth":    binomialCoeff(8, min(2, len(chunks))),
		"hypersimplex_dimension": 7,
	}
}

// Helper functions
func getString(m map[string]interface{}, key string, def ...string) string {
	if val, ok := m[key].(string); ok {
		return val
	}
	if len(def) > 0 {
		return def[0]
	}
	return ""
}

func getInt(m map[string]interface{}, key string) int {
	if val, ok := m[key].(float64); ok {
		return int(val)
	}
	return 0
}

func getFloat64(m map[string]interface{}, key string) float64 {
	if val, ok := m[key].(float64); ok {
		return val
	}
	return 0.0
}

func hashString(s string) int64 {
	h := int64(0)
	for _, c := range s {
		h = 31*h + int64(c)
	}
	return h
}

func binomialCoeff(n, k int) int {
	if k < 0 || k > n {
		return 0
	}
	if k == 0 || k == n {
		return 1
	}
	k = min(k, n-k)
	res := 1
	for i := 1; i <= k; i++ {
		res = res * (n - k + i) / i
	}
	return res
}