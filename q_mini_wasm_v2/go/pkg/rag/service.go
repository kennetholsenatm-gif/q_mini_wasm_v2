package rag

import (
	"context"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

// ServiceConfig holds RAG service configuration
type ServiceConfig struct {
	Qdrant      QdrantConfig      `json:"qdrant"`
	Chunker     ChunkConfig       `json:"chunker"`
	Scaler      TokenScalerConfig `json:"scaler"`
	ProjectRoot string            `json:"project_root"`
	AutoIndex   bool              `json:"auto_index"`
}

// Service is the main RAG service
type Service struct {
	config    ServiceConfig
	qdrant    *QdrantClient
	chunker   *Chunker
	scaler    *TokenScaler
	mu        sync.RWMutex
	metrics   *Metrics
	isRunning bool
}

// Metrics tracks RAG performance
type Metrics struct {
	TotalDocuments   int64   `json:"total_documents"`
	TotalChunks      int64   `json:"total_chunks"`
	TotalQueries     int64   `json:"total_queries"`
	AvgLatencyMs     float64 `json:"avg_latency_ms"`
	AvgTokensSaved   float64 `json:"avg_tokens_saved"`
	CacheHitRate     float64 `json:"cache_hit_rate"`
	mu               sync.RWMutex
}

// NewService creates a new RAG service
func NewService(config ServiceConfig) (*Service, error) {
	// Create Qdrant client
	qdrant, err := NewQdrantClient(config.Qdrant)
	if err != nil {
		return nil, fmt.Errorf("failed to create Qdrant client: %w", err)
	}

	// Create chunker
	chunker := NewChunker(config.Chunker)

	// Create scaler
	scaler := NewTokenScaler(config.Scaler)

	service := &Service{
		config:  config,
		qdrant:  qdrant,
		chunker: chunker,
		scaler:  scaler,
		metrics: &Metrics{},
	}

	return service, nil
}

// Start starts the RAG service
func (s *Service) Start(ctx context.Context) error {
	s.mu.Lock()
	if s.isRunning {
		s.mu.Unlock()
		return fmt.Errorf("service already running")
	}
	s.isRunning = true
	s.mu.Unlock()

	log.Println("Starting RAG service...")

	// Initial indexing
	if s.config.AutoIndex {
		log.Println("Performing initial indexing...")
		if err := s.IndexDirectory(ctx, s.config.ProjectRoot); err != nil {
			log.Printf("Initial indexing failed: %v", err)
		}
	}

	log.Println("RAG service started successfully")
	return nil
}

// Stop stops the RAG service
func (s *Service) Stop() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if !s.isRunning {
		return nil
	}

	s.isRunning = false

	log.Println("RAG service stopped")
	return nil
}

// RetrieveContext retrieves relevant context for a query
func (s *Service) RetrieveContext(ctx context.Context, query string, maxTokens int, contextType string) (*RetrieveResult, error) {
	start := time.Now()

	// Scale tokens
	scaleResult := s.scaler.Scale(ScaleRequest{
		Query:       query,
		MaxTokens:   maxTokens,
		ContextType: contextType,
	})

	// Generate query embedding (placeholder - needs actual embedding model)
	// In production, this would call an embedding service
	queryVector := s.generatePlaceholderEmbedding(query)

	// Search Qdrant
	results, err := s.qdrant.Search(ctx, queryVector, 10, 0.5)
	if err != nil {
		return nil, fmt.Errorf("search failed: %w", err)
	}

	// Build response within token budget
	var chunks []ContextChunkInfo
	totalTokens := 0

	for _, r := range results {
		if totalTokens+r.TokenCount() > scaleResult.RecommendedTokens {
			break
		}

		chunks = append(chunks, ContextChunkInfo{
			Content:    r.Content,
			SourceFile: r.SourceFile,
			StartLine:  r.StartLine,
			EndLine:    r.EndLine,
			Score:      r.Score,
			ChunkType:  r.ChunkType,
		})
		totalTokens += r.TokenCount()
	}

	// Update metrics
	latency := time.Since(start).Milliseconds()
	s.updateMetrics(latency, totalTokens, scaleResult.RecommendedTokens)

	return &RetrieveResult{
		Chunks:         chunks,
		TotalTokens:    totalTokens,
		ScalingInfo:    scaleResult,
		LatencyMs:      latency,
		QueryComplexity: scaleResult.ComplexityScore,
	}, nil
}

// IndexDocument indexes a single document
func (s *Service) IndexDocument(ctx context.Context, filePath string) error {
	// Read file
	content, err := os.ReadFile(filePath)
	if err != nil {
		return fmt.Errorf("failed to read file: %w", err)
	}

	// Determine tags
	tags := s.determineTags(filePath)

	// Chunk document
	chunks, err := s.chunker.ChunkDocument(filePath, string(content), tags)
	if err != nil {
		return fmt.Errorf("chunking failed: %w", err)
	}

	// Generate embeddings and upsert
	points := make([]VectorPoint, len(chunks))
	for i, chunk := range chunks {
		embedding := s.generatePlaceholderEmbedding(chunk.Content)
		points[i] = VectorPoint{
			ID:     chunk.ID,
			Vector: embedding,
			Payload: map[string]interface{}{
				"content":     chunk.Content,
				"source_file": chunk.SourceFile,
				"start_line":  chunk.StartLine,
				"end_line":    chunk.EndLine,
				"chunk_type":  chunk.ChunkType,
				"tags":        chunk.Tags,
				"token_count": chunk.TokenCount,
			},
		}
	}

	if err := s.qdrant.UpsertPoints(ctx, points); err != nil {
		return fmt.Errorf("upsert failed: %w", err)
	}

	// Update metrics
	s.metrics.mu.Lock()
	s.metrics.TotalDocuments++
	s.metrics.TotalChunks += int64(len(chunks))
	s.metrics.mu.Unlock()

	log.Printf("Indexed %s: %d chunks", filePath, len(chunks))
	return nil
}

// IndexDirectory indexes all files in a directory
func (s *Service) IndexDirectory(ctx context.Context, dir string) error {
	return filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		// Skip directories and non-relevant files
		if info.IsDir() {
			return nil
		}

		// Check file extension
		ext := strings.ToLower(filepath.Ext(path))
		validExts := []string{".cpp", ".hpp", ".c", ".h", ".go", ".rs", ".py", ".md", ".txt"}
		valid := false
		for _, ve := range validExts {
			if ext == ve {
				valid = true
				break
			}
		}
		if !valid {
			return nil
		}

		// Skip hidden files and directories
		if strings.HasPrefix(filepath.Base(path), ".") {
			return nil
		}

		// Index file
		if err := s.IndexDocument(ctx, path); err != nil {
			log.Printf("Failed to index %s: %v", path, err)
		}

		return nil
	})
}

// determineTags determines tags for a file
func (s *Service) determineTags(filePath string) []string {
	var tags []string

	// File type tags
	ext := strings.ToLower(filepath.Ext(filePath))
	switch ext {
	case ".cpp", ".hpp", ".c", ".h":
		tags = append(tags, "lang:cpp")
	case ".go":
		tags = append(tags, "lang:go")
	case ".rs":
		tags = append(tags, "lang:rust")
	case ".md":
		tags = append(tags, "type:documentation")
	}

	// Directory-based tags
	relPath, _ := filepath.Rel(s.config.ProjectRoot, filePath)
	parts := strings.Split(relPath, string(filepath.Separator))
	if len(parts) > 0 {
		tags = append(tags, "dir:"+parts[0])
	}

	return tags
}

// generatePlaceholderEmbedding generates a placeholder embedding
// In production, this would call an actual embedding model
func (s *Service) generatePlaceholderEmbedding(text string) []float32 {
	// Simple hash-based placeholder
	// Replace with actual embedding model in production
	vector := make([]float32, 384) // Common embedding size
	for i := range vector {
		vector[i] = float32(len(text)%100) / 100.0
	}
	return vector
}

// updateMetrics updates service metrics
func (s *Service) updateMetrics(latencyMs int64, tokensUsed, tokensBudget int) {
	s.metrics.mu.Lock()
	defer s.metrics.mu.Unlock()

	s.metrics.TotalQueries++
	
	// Update average latency
	s.metrics.AvgLatencyMs = (s.metrics.AvgLatencyMs*float64(s.metrics.TotalQueries-1) + float64(latencyMs)) / float64(s.metrics.TotalQueries)
	
	// Update tokens saved
	saved := float64(tokensBudget - tokensUsed)
	if saved > 0 {
		s.metrics.AvgTokensSaved = (s.metrics.AvgTokensSaved*float64(s.metrics.TotalQueries-1) + saved) / float64(s.metrics.TotalQueries)
	}
}

// GetMetrics returns current metrics
func (s *Service) GetMetrics() Metrics {
	s.metrics.mu.RLock()
	defer s.metrics.mu.RUnlock()
	return *s.metrics
}

// RetrieveResult represents context retrieval result
type RetrieveResult struct {
	Chunks          []ContextChunkInfo `json:"chunks"`
	TotalTokens     int                `json:"total_tokens"`
	ScalingInfo     ScaleResult        `json:"scaling_info"`
	LatencyMs       int64              `json:"latency_ms"`
	QueryComplexity float64            `json:"query_complexity"`
}

// ContextChunkInfo represents a retrieved context chunk
type ContextChunkInfo struct {
	Content    string  `json:"content"`
	SourceFile string  `json:"source_file"`
	StartLine  int32   `json:"start_line"`
	EndLine    int32   `json:"end_line"`
	Score      float32 `json:"score"`
	ChunkType  string  `json:"chunk_type"`
}

// TokenCount returns estimated token count for a search result
func (sr SearchResult) TokenCount() int {
	return len(sr.Content) / 4
}