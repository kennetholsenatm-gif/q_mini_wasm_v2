package rag

import (
	"context"
	"math"
	"sort"
	"strings"
	"sync"
)

// HybridSearchConfig holds hybrid search configuration
type HybridSearchConfig struct {
	SemanticWeight  float64 `json:"semantic_weight"`   // Weight for semantic search (0-1)
	KeywordWeight   float64 `json:"keyword_weight"`    // Weight for keyword search (0-1)
	RerankTopK      int     `json:"rerank_top_k"`      // Number of results to rerank
	MinScore        float32 `json:"min_score"`         // Minimum score threshold
}

// HybridSearchService combines semantic and keyword search
type HybridSearchService struct {
	config          HybridSearchConfig
	embeddingService EmbeddingService
	qdrant          *QdrantClient
	mu              sync.RWMutex
}

// NewHybridSearchService creates a new hybrid search service
func NewHybridSearchService(config HybridSearchConfig, embeddingService EmbeddingService, qdrant *QdrantClient) *HybridSearchService {
	if config.SemanticWeight == 0 && config.KeywordWeight == 0 {
		config.SemanticWeight = 0.7
		config.KeywordWeight = 0.3
	}
	if config.RerankTopK == 0 {
		config.RerankTopK = 20
	}
	if config.MinScore == 0 {
		config.MinScore = 0.3
	}
	
	// Normalize weights
	total := config.SemanticWeight + config.KeywordWeight
	config.SemanticWeight /= total
	config.KeywordWeight /= total
	
	return &HybridSearchService{
		config:           config,
		embeddingService: embeddingService,
		qdrant:           qdrant,
	}
}

// SearchResultWithScore represents a search result with combined score
type SearchResultWithScore struct {
	Result       SearchResult
	SemanticScore float32
	KeywordScore  float32
	CombinedScore float32
}

// Search performs hybrid search combining semantic and keyword search
func (h *HybridSearchService) Search(ctx context.Context, query string, topK int, threshold float32) ([]SearchResultWithScore, error) {
	// 1. Semantic search
	semanticResults, err := h.semanticSearch(ctx, query, h.config.RerankTopK)
	if err != nil {
		return nil, err
	}
	
	// 2. Keyword search
	keywordResults := h.keywordSearch(query, semanticResults)
	
	// 3. Combine and rerank results
	combined := h.combineResults(semanticResults, keywordResults)
	
	// 4. Apply threshold and limit
	filtered := h.filterAndLimit(combined, threshold, topK)
	
	return filtered, nil
}

// semanticSearch performs vector similarity search
func (h *HybridSearchService) semanticSearch(ctx context.Context, query string, topK int) ([]SearchResult, error) {
	// Generate query embedding
	queryEmbedding, err := h.embeddingService.Embed(ctx, query)
	if err != nil {
		return nil, err
	}
	
	// Search Qdrant
	results, err := h.qdrant.Search(ctx, queryEmbedding, uint64(topK), 0.0)
	if err != nil {
		return nil, err
	}
	
	return results, nil
}

// keywordSearch performs keyword-based search (BM25-like scoring)
func (h *HybridSearchService) keywordSearch(query string, candidates []SearchResult) map[string]float32 {
	scores := make(map[string]float32)
	
	// Tokenize query
	queryTokens := tokenize(query)
	if len(queryTokens) == 0 {
		return scores
	}
	
	// Calculate BM25-like scores for each candidate
	for _, candidate := range candidates {
		score := calculateBM25Score(queryTokens, candidate.Content)
		scores[candidate.ID] = score
	}
	
	return scores
}

// tokenize splits text into tokens
func tokenize(text string) []string {
	// Simple tokenization: lowercase and split on whitespace/punctuation
	text = strings.ToLower(text)
	
	// Remove common punctuation
	replacer := strings.NewReplacer(
		".", " ", ",", " ", ";", " ", ":", " ",
		"(", " ", ")", " ", "[", " ", "]", " ",
		"{", " ", "}", " ", "\"", " ", "'", " ",
	)
	text = replacer.Replace(text)
	
	// Split and filter empty strings
	words := strings.Fields(text)
	var tokens []string
	for _, word := range words {
		if len(word) > 1 { // Filter single characters
			tokens = append(tokens, word)
		}
	}
	
	return tokens
}

// calculateBM25Score calculates a BM25-like score
func calculateBM25Score(queryTokens []string, document string) float32 {
	if len(queryTokens) == 0 {
		return 0
	}
	
	docTokens := tokenize(document)
	if len(docTokens) == 0 {
		return 0
	}
	
	// Count term frequencies in document
	docTermFreq := make(map[string]int)
	for _, token := range docTokens {
		docTermFreq[token]++
	}
	
	// BM25 parameters
	k1 := float32(1.5)
	b := float32(0.75)
	
	// Calculate average document length (simplified)
	avgDocLen := float32(100) // Assume average document length
	
	score := float32(0)
	
	for _, queryToken := range queryTokens {
		if freq, exists := docTermFreq[queryToken]; exists {
			// TF component
			tf := float32(freq)
			docLen := float32(len(docTokens))
			
			// BM25 TF normalization
			tfNorm := (tf * (k1 + 1)) / (tf + k1*(1-b+b*(docLen/avgDocLen)))
			
			// IDF component (simplified - assume document frequency of 1)
			idf := float32(1.0) // In real implementation, calculate based on corpus
			
			score += tfNorm * idf
		}
	}
	
	// Normalize score to [0, 1]
	if score > 0 {
		score = score / (score + 1)
	}
	
	return score
}

// combineResults combines semantic and keyword scores
func (h *HybridSearchService) combineResults(semanticResults []SearchResult, keywordScores map[string]float32) []SearchResultWithScore {
	combined := make([]SearchResultWithScore, 0, len(semanticResults))
	
	for _, result := range semanticResults {
		semanticScore := result.Score
		keywordScore := float32(0)
		if score, exists := keywordScores[result.ID]; exists {
			keywordScore = score
		}
		
		// Calculate combined score
		combinedScore := float32(h.config.SemanticWeight)*semanticScore + 
			float32(h.config.KeywordWeight)*keywordScore
		
		combined = append(combined, SearchResultWithScore{
			Result:        result,
			SemanticScore: semanticScore,
			KeywordScore:  keywordScore,
			CombinedScore: combinedScore,
		})
	}
	
	// Sort by combined score (descending)
	sort.Slice(combined, func(i, j int) bool {
		return combined[i].CombinedScore > combined[j].CombinedScore
	})
	
	return combined
}

// filterAndLimit filters results by threshold and limits to topK
func (h *HybridSearchService) filterAndLimit(results []SearchResultWithScore, threshold float32, topK int) []SearchResultWithScore {
	var filtered []SearchResultWithScore
	
	for _, result := range results {
		if result.CombinedScore >= threshold {
			filtered = append(filtered, result)
		}
	}
	
	if len(filtered) > topK {
		filtered = filtered[:topK]
	}
	
	return filtered
}

// ============================================================================
// Reranker for improving search relevance
// ============================================================================

// RerankerConfig holds reranker configuration
type RerankerConfig struct {
	ModelPath      string  `json:"model_path"`       // Path to reranker model
	BatchSize      int     `json:"batch_size"`        // Batch size for inference
	MaxSeqLength   int     `json:"max_seq_length"`    // Maximum sequence length
	ScoreThreshold float32 `json:"score_threshold"`   // Minimum score threshold
}

// Reranker improves search result relevance
type Reranker struct {
	config RerankerConfig
}

// NewReranker creates a new reranker
func NewReranker(config RerankerConfig) *Reranker {
	if config.BatchSize == 0 {
		config.BatchSize = 32
	}
	if config.MaxSeqLength == 0 {
		config.MaxSeqLength = 512
	}
	if config.ScoreThreshold == 0 {
		config.ScoreThreshold = 0.5
	}
	
	return &Reranker{config: config}
}

// Rerank reranks search results based on query-document relevance
func (r *Reranker) Rerank(ctx context.Context, query string, results []SearchResultWithScore) ([]SearchResultWithScore, error) {
	if len(results) == 0 {
		return results, nil
	}
	
	// Simple heuristic reranking based on query-document overlap
	// In production, this would use a trained reranker model
	reranked := make([]SearchResultWithScore, len(results))
	copy(reranked, results)
	
	queryTokens := tokenize(query)
	
	for i := range reranked {
		docTokens := tokenize(reranked[i].Result.Content)
		
		// Calculate overlap score
		overlapScore := calculateOverlapScore(queryTokens, docTokens)
		
		// Adjust combined score with overlap
		reranked[i].CombinedScore = reranked[i].CombinedScore*0.7 + overlapScore*0.3
	}
	
	// Re-sort by adjusted score
	sort.Slice(reranked, func(i, j int) bool {
		return reranked[i].CombinedScore > reranked[j].CombinedScore
	})
	
	return reranked, nil
}

// calculateOverlapScore calculates token overlap between query and document
func calculateOverlapScore(queryTokens, docTokens []string) float32 {
	if len(queryTokens) == 0 || len(docTokens) == 0 {
		return 0
	}
	
	// Create set of document tokens
	docSet := make(map[string]bool)
	for _, token := range docTokens {
		docSet[token] = true
	}
	
	// Count matches
	matches := 0
	for _, token := range queryTokens {
		if docSet[token] {
			matches++
		}
	}
	
	// Calculate overlap score (Jaccard similarity)
	union := len(queryTokens) + len(docTokens) - matches
	if union == 0 {
		return 0
	}
	
	return float32(matches) / float32(union)
}

// ============================================================================
// Query Expansion
// ============================================================================

// QueryExpander expands queries for better retrieval
type QueryExpander struct {
	embeddingService EmbeddingService
}

// NewQueryExpander creates a new query expander
func NewQueryExpander(embeddingService EmbeddingService) *QueryExpander {
	return &QueryExpander{embeddingService: embeddingService}
}

// ExpandQuery generates query variations for better retrieval
func (qe *QueryExpander) ExpandQuery(ctx context.Context, query string) ([]string, error) {
	variations := []string{query}
	
	// Simple query expansion strategies
	// 1. Add synonyms (simplified)
	expanded := expandWithSynonyms(query)
	if expanded != query {
		variations = append(variations, expanded)
	}
	
	// 2. Add related terms
	related := addRelatedTerms(query)
	if related != query {
		variations = append(variations, related)
	}
	
	// 3. Reformulate question
	if strings.Contains(query, "?") {
		reformulated := reformulateQuestion(query)
		if reformulated != query {
			variations = append(variations, reformulated)
		}
	}
	
	return variations, nil
}

// expandWithSynonyms adds synonyms to query
func expandWithSynonyms(query string) string {
	// Simplified synonym expansion
	// In production, use a synonym dictionary or word embeddings
	synonyms := map[string][]string{
		"function": {"method", "procedure", "func"},
		"class":    {"type", "struct", "interface"},
		"error":    {"bug", "issue", "problem"},
		"create":   {"make", "build", "generate"},
		"delete":   {"remove", "destroy", "erase"},
	}
	
	words := strings.Fields(strings.ToLower(query))
	var expanded []string
	
	for _, word := range words {
		expanded = append(expanded, word)
		if syns, ok := synonyms[word]; ok && len(syns) > 0 {
			expanded = append(expanded, syns[0])
		}
	}
	
	return strings.Join(expanded, " ")
}

// addRelatedTerms adds related terms to query
func addRelatedTerms(query string) string {
	// Add context-specific terms
	if strings.Contains(strings.ToLower(query), "error") {
		return query + " exception handling"
	}
	if strings.Contains(strings.ToLower(query), "performance") {
		return query + " optimization"
	}
	return query
}

// reformulateQuestion reformulates questions
func reformulateQuestion(query string) string {
	// Convert "how to" questions to "what is" format
	query = strings.TrimSpace(query)
	if strings.HasPrefix(strings.ToLower(query), "how to ") {
		return "what is " + strings.TrimPrefix(strings.ToLower(query), "how to ")
	}
	return query
}
