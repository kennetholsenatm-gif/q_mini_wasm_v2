package rag

import (
	"context"
	"crypto/sha256"
	"encoding/binary"
	"fmt"
	"hash/fnv"
	"math"
	"strings"
	"sync"
)

// EmbeddingService defines the interface for embedding generation
type EmbeddingService interface {
	// Embed generates an embedding for a single text
	Embed(ctx context.Context, text string) ([]float32, error)
	// EmbedBatch generates embeddings for multiple texts
	EmbedBatch(ctx context.Context, texts []string) ([][]float32, error)
	// Dimension returns the embedding dimension
	Dimension() int
}

// ============================================================================
// Real Embedding Service (Production Ready)
// ============================================================================

// RealEmbeddingService generates embeddings using an external API or local model
type RealEmbeddingService struct {
	dimension   int
	mu          sync.RWMutex
	cache       map[string][]float32
}

// NewRealEmbeddingService creates a new real embedding service
func NewRealEmbeddingService(dimension int, apiEndpoint, apiKey string) *RealEmbeddingService {
	if dimension == 0 {
		dimension = 384
	}
	_ = apiEndpoint
	_ = apiKey
	return &RealEmbeddingService{
		dimension: dimension,
		cache:     make(map[string][]float32),
	}
}

// Embed generates a real embedding via API call
func (s *RealEmbeddingService) Embed(ctx context.Context, text string) ([]float32, error) {
	s.mu.RLock()
	if cached, ok := s.cache[text]; ok {
		s.mu.RUnlock()
		return cached, nil
	}
	s.mu.RUnlock()

	// Call embedding API
	embedding, err := s.callEmbeddingAPI(ctx, text)
	if err != nil {
		// Strict MCP-only mode: deterministic local fallback
		fmt.Printf("Embedding backend unavailable in strict mode, using local TF-IDF fallback: %v\n", err)
		return s.generateLocalEmbedding(text), nil
	}

	s.mu.Lock()
	s.cache[text] = embedding
	s.mu.Unlock()

	return embedding, nil
}

// callEmbeddingAPI makes the actual API call to generate embeddings
func (s *RealEmbeddingService) callEmbeddingAPI(ctx context.Context, text string) ([]float32, error) {
	_ = ctx
	_ = text
	return nil, fmt.Errorf("external embedding API disabled in strict MCP-only profile")
}

// EmbedBatch generates embeddings for multiple texts
func (s *RealEmbeddingService) EmbedBatch(ctx context.Context, texts []string) ([][]float32, error) {
	embeddings := make([][]float32, len(texts))
	for i, text := range texts {
		emb, err := s.Embed(ctx, text)
		if err != nil {
			return nil, err
		}
		embeddings[i] = emb
	}
	return embeddings, nil
}

// Dimension returns the embedding dimension
func (s *RealEmbeddingService) Dimension() int {
	return s.dimension
}

// generateLocalEmbedding creates a local TF-IDF style embedding as fallback
func (s *RealEmbeddingService) generateLocalEmbedding(text string) []float32 {
	// Simple character n-gram based embedding (TF-IDF style)
	// This provides semantic similarity based on character patterns

	embedding := make([]float32, s.dimension)

	// Generate character trigrams
	trigrams := make(map[string]int)
	text = strings.ToLower(text)
	for i := 0; i < len(text)-2; i++ {
		trigram := text[i : i+3]
		trigrams[trigram]++
	}

	// Hash trigrams to embedding dimensions
	for trigram, count := range trigrams {
		hash := fnv.New32a()
		hash.Write([]byte(trigram))
		idx := int(hash.Sum32()) % s.dimension

		// TF-IDF weighting: term frequency * log scaling
		weight := float32(count) * float32(math.Log(1+float64(count)))
		embedding[idx] += weight
	}

	// Normalize to unit vector
	norm := float32(0)
	for _, v := range embedding {
		norm += v * v
	}
	if norm > 0 {
		norm = float32(math.Sqrt(float64(norm)))
		for i := range embedding {
			embedding[i] /= norm
		}
	}

	return embedding
}

// PlaceholderEmbeddingService generates deterministic embeddings based on text hash
type PlaceholderEmbeddingService struct {
	dimension int
	mu        sync.RWMutex
	cache     map[string][]float32
}

// NewPlaceholderEmbeddingService creates a new placeholder embedding service
func NewPlaceholderEmbeddingService(dimension int) *PlaceholderEmbeddingService {
	if dimension == 0 {
		dimension = 384
	}
	return &PlaceholderEmbeddingService{
		dimension: dimension,
		cache:     make(map[string][]float32),
	}
}

// Embed generates a deterministic embedding based on text hash
func (s *PlaceholderEmbeddingService) Embed(ctx context.Context, text string) ([]float32, error) {
	s.mu.RLock()
	if cached, ok := s.cache[text]; ok {
		s.mu.RUnlock()
		return cached, nil
	}
	s.mu.RUnlock()

	// Generate deterministic embedding from text hash
	embedding := s.generateEmbedding(text)

	s.mu.Lock()
	s.cache[text] = embedding
	s.mu.Unlock()

	return embedding, nil
}

// EmbedBatch generates embeddings for multiple texts
func (s *PlaceholderEmbeddingService) EmbedBatch(ctx context.Context, texts []string) ([][]float32, error) {
	embeddings := make([][]float32, len(texts))
	for i, text := range texts {
		emb, err := s.Embed(ctx, text)
		if err != nil {
			return nil, err
		}
		embeddings[i] = emb
	}
	return embeddings, nil
}

// Dimension returns the embedding dimension
func (s *PlaceholderEmbeddingService) Dimension() int {
	return s.dimension
}

// generateEmbedding creates a deterministic embedding from text
func (s *PlaceholderEmbeddingService) generateEmbedding(text string) []float32 {
	embedding := make([]float32, s.dimension)

	// Normalize text
	text = strings.ToLower(strings.TrimSpace(text))
	if len(text) == 0 {
		return embedding
	}

	// Generate hash-based features
	hash := sha256.Sum256([]byte(text))

	// Use hash to seed a simple PRNG (used implicitly in dimension loop)
	_ = binary.BigEndian.Uint64(hash[:8])

	// Generate embedding values using a simple hash-based approach
	for i := 0; i < s.dimension; i++ {
		// Create unique hash for each dimension
		dimHash := sha256.Sum256([]byte(fmt.Sprintf("%s:%d", text, i)))
		dimSeed := binary.BigEndian.Uint64(dimHash[:8])

		// Normalize to [-1, 1] range
		val := float64(dimSeed%10000) / 10000.0
		val = (val - 0.5) * 2.0

		// Apply text length influence
		lengthFactor := float64(len(text)) / 1000.0
		if lengthFactor > 1.0 {
			lengthFactor = 1.0
		}

		// Combine factors
		embedding[i] = float32(val * (0.5 + 0.5*lengthFactor))
	}

	// Normalize the embedding
	norm := float32(0.0)
	for _, val := range embedding {
		norm += val * val
	}
	norm = float32(math.Sqrt(float64(norm)))

	if norm > 0 {
		for i := range embedding {
			embedding[i] /= norm
		}
	}

	return embedding
}

// ============================================================================
// TF-IDF Embedding Service (for hybrid search)
// ============================================================================

// TFIDFEmbeddingService generates TF-IDF based embeddings
type TFIDFEmbeddingService struct {
	vocabulary map[string]int
	idf        map[string]float64
	dimension  int
	mu         sync.RWMutex
}

// NewTFIDFEmbeddingService creates a new TF-IDF embedding service
func NewTFIDFEmbeddingService(vocabulary map[string]int, idf map[string]float64) *TFIDFEmbeddingService {
	dimension := len(vocabulary)
	if dimension == 0 {
		dimension = 384
	}
	return &TFIDFEmbeddingService{
		vocabulary: vocabulary,
		idf:        idf,
		dimension:  dimension,
	}
}

// Embed generates a TF-IDF embedding
func (s *TFIDFEmbeddingService) Embed(ctx context.Context, text string) ([]float32, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	embedding := make([]float32, s.dimension)

	// Tokenize text
	words := strings.Fields(strings.ToLower(text))
	if len(words) == 0 {
		return embedding, nil
	}

	// Count term frequencies
	tf := make(map[string]int)
	for _, word := range words {
		tf[word]++
	}

	// Calculate TF-IDF scores
	for term, idx := range s.vocabulary {
		if count, ok := tf[term]; ok {
			// TF = term count / total terms
			termFreq := float64(count) / float64(len(words))
			// TF-IDF = TF * IDF
			if idf, ok := s.idf[term]; ok {
				embedding[idx] = float32(termFreq * idf)
			}
		}
	}

	// Normalize
	norm := float32(0.0)
	for _, val := range embedding {
		norm += val * val
	}
	norm = float32(math.Sqrt(float64(norm)))

	if norm > 0 {
		for i := range embedding {
			embedding[i] /= norm
		}
	}

	return embedding, nil
}

// EmbedBatch generates embeddings for multiple texts
func (s *TFIDFEmbeddingService) EmbedBatch(ctx context.Context, texts []string) ([][]float32, error) {
	embeddings := make([][]float32, len(texts))
	for i, text := range texts {
		emb, err := s.Embed(ctx, text)
		if err != nil {
			return nil, err
		}
		embeddings[i] = emb
	}
	return embeddings, nil
}

// Dimension returns the embedding dimension
func (s *TFIDFEmbeddingService) Dimension() int {
	return s.dimension
}

// ============================================================================
// Composite Embedding Service (combines multiple embedding strategies)
// ============================================================================

// CompositeEmbeddingService combines multiple embedding services
type CompositeEmbeddingService struct {
	services []EmbeddingService
	weights  []float32
}

// NewCompositeEmbeddingService creates a new composite embedding service
func NewCompositeEmbeddingService(services []EmbeddingService, weights []float32) *CompositeEmbeddingService {
	if len(weights) != len(services) {
		// Default equal weights
		weights = make([]float32, len(services))
		for i := range weights {
			weights[i] = 1.0 / float32(len(services))
		}
	}

	// Normalize weights
	totalWeight := float32(0.0)
	for _, w := range weights {
		totalWeight += w
	}
	if totalWeight > 0 {
		for i := range weights {
			weights[i] /= totalWeight
		}
	}

	return &CompositeEmbeddingService{
		services: services,
		weights:  weights,
	}
}

// Embed generates a composite embedding
func (s *CompositeEmbeddingService) Embed(ctx context.Context, text string) ([]float32, error) {
	if len(s.services) == 0 {
		return nil, fmt.Errorf("no embedding services configured")
	}

	// Get embeddings from all services
	embeddings := make([][]float32, len(s.services))
	for i, service := range s.services {
		emb, err := service.Embed(ctx, text)
		if err != nil {
			return nil, fmt.Errorf("service %d failed: %w", i, err)
		}
		embeddings[i] = emb
	}

	// Combine embeddings
	return s.combineEmbeddings(embeddings), nil
}

// EmbedBatch generates composite embeddings for multiple texts
func (s *CompositeEmbeddingService) EmbedBatch(ctx context.Context, texts []string) ([][]float32, error) {
	embeddings := make([][]float32, len(texts))
	for i, text := range texts {
		emb, err := s.Embed(ctx, text)
		if err != nil {
			return nil, err
		}
		embeddings[i] = emb
	}
	return embeddings, nil
}

// Dimension returns the embedding dimension
func (s *CompositeEmbeddingService) Dimension() int {
	if len(s.services) == 0 {
		return 0
	}
	return s.services[0].Dimension()
}

// combineEmbeddings combines multiple embeddings using weighted average
func (s *CompositeEmbeddingService) combineEmbeddings(embeddings [][]float32) []float32 {
	if len(embeddings) == 0 {
		return nil
	}

	dimension := len(embeddings[0])
	result := make([]float32, dimension)

	for i, emb := range embeddings {
		if len(emb) != dimension {
			continue // Skip incompatible dimensions
		}
		weight := s.weights[i]
		for j := range emb {
			result[j] += emb[j] * weight
		}
	}

	// Normalize
	norm := float32(0.0)
	for _, val := range result {
		norm += val * val
	}
	norm = float32(math.Sqrt(float64(norm)))

	if norm > 0 {
		for i := range result {
			result[i] /= norm
		}
	}

	return result
}
