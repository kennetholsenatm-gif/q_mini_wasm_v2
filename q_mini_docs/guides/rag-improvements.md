# RAG System Improvements

## Overview

This document describes the improvements made to the Retrieval-Augmented Generation (RAG) system in the q_mini_wasm_v2 project.

## Key Improvements

### 1. Embedding Service Interface (`embedding.go`)

**Problem**: The original implementation used a placeholder embedding that returned identical values for all dimensions, making semantic search ineffective.

**Solution**: Created a pluggable embedding service interface with multiple implementations:

- **PlaceholderEmbeddingService**: Generates deterministic embeddings based on text hash (for testing)
- **TFIDFEmbeddingService**: TF-IDF based embeddings for keyword-focused search
- **CompositeEmbeddingService**: Combines multiple embedding services with weighted averaging

**Benefits**:
- Modular design allows easy integration of external embedding models (OpenAI, Cohere, local ONNX models)
- Deterministic placeholder ensures reproducible results during development
- Caching at the embedding level reduces redundant computation

### 2. Hybrid Search (`hybrid_search.go`)

**Problem**: Only semantic vector search was used, missing exact keyword matches.

**Solution**: Implemented hybrid search combining:

- **Semantic Search**: Vector similarity using Qdrant
- **Keyword Search**: BM25-like scoring for term frequency matching
- **Configurable Weights**: Adjust balance between semantic (default 70%) and keyword (default 30%) results
- **Reranking**: Post-retrieval reranking based on query-document overlap

**Benefits**:
- Better recall for exact term matches (function names, error codes)
- Improved relevance through combined scoring
- Fallback to semantic search if hybrid search fails

### 3. Caching Layer (`cache.go`)

**Problem**: No caching led to repeated expensive operations for identical queries and embeddings.

**Solution**: Implemented two-level caching:

- **EmbeddingCache**: Caches generated embeddings
- **QueryCache**: Caches complete query results
- **LRU/LFU/FIFO Eviction**: Configurable eviction policies
- **TTL Support**: Time-based cache expiration

**Benefits**:
- Reduced latency for repeated queries
- Lower computational costs
- Configurable cache size and policies

### 4. Enhanced Service Architecture (`service.go`)

**Changes**:
- Integrated all new components
- Added comprehensive metrics tracking (cache hits/misses)
- Improved error handling with fallback mechanisms
- Better configuration management

## Configuration

### ServiceConfig Structure

```go
type ServiceConfig struct {
    Qdrant        QdrantConfig        // Vector database configuration
    Chunker       ChunkConfig         // Document chunking settings
    Scaler        TokenScalerConfig   // Token budget scaling
    HybridSearch  HybridSearchConfig  // Hybrid search weights
    Cache         CacheConfig         // Caching configuration
    ProjectRoot   string              // Root directory for indexing
    AutoIndex     bool                // Auto-index on startup
    EmbeddingType string              // "placeholder", "tfidf", "composite"
}
```

### HybridSearchConfig

```go
type HybridSearchConfig struct {
    SemanticWeight  float64  // Weight for semantic search (0-1)
    KeywordWeight   float64  // Weight for keyword search (0-1)
    RerankTopK      int      // Number of results to rerank
    MinScore        float32  // Minimum score threshold
}
```

### CacheConfig

```go
type CacheConfig struct {
    MaxSize        int           // Maximum number of items
    TTL            time.Duration // Time to live
    EvictionPolicy string        // "lru", "lfu", "fifo"
}
```

## Usage Examples

### Basic Configuration

```go
config := rag.ServiceConfig{
    Qdrant: rag.QdrantConfig{
        Host:           "localhost",
        Port:           6334,
        CollectionName: "qminiwasm_docs",
        VectorSize:     384,
    },
    HybridSearch: rag.HybridSearchConfig{
        SemanticWeight: 0.7,
        KeywordWeight:  0.3,
    },
    Cache: rag.CacheConfig{
        MaxSize:        1000,
        TTL:            5 * time.Minute,
        EvictionPolicy: "lru",
    },
    EmbeddingType: "placeholder",
}

service, err := rag.NewService(config)
```

### Querying with Hybrid Search

```go
result, err := service.RetrieveContext(ctx, "How to implement quantum gates?", 2048, "code")
if err != nil {
    log.Fatal(err)
}

for _, chunk := range result.Chunks {
    fmt.Printf("Score: %.2f, File: %s, Lines: %d-%d\n", 
        chunk.Score, chunk.SourceFile, chunk.StartLine, chunk.EndLine)
}
```

## Metrics

The service now tracks enhanced metrics:

```go
type Metrics struct {
    TotalDocuments   int64    // Total indexed documents
    TotalChunks      int64    // Total indexed chunks
    TotalQueries     int64    // Total queries served
    CacheHits        int64    // Cache hit count
    CacheMisses      int64    // Cache miss count
    AvgLatencyMs     float64  // Average query latency
    AvgTokensSaved   float64  // Average tokens saved
    CacheHitRate     float64  // Cache hit rate percentage
}
```

## Future Improvements

1. **External Embedding Integration**: Add support for OpenAI, Cohere, or local ONNX models
2. **BM25 Index**: Full BM25 implementation with document frequency tracking
3. **Query Expansion**: Use LLM to expand queries for better retrieval
4. **Semantic Chunking**: Use embedding similarity for smarter document splitting
5. **Distributed Caching**: Redis-based caching for multi-instance deployments

## API Endpoints

- `POST /api/v1/rag/retrieve` - Retrieve context for a query
- `GET /api/v1/rag/metrics` - Get RAG service metrics
- `POST /api/v1/rag/index` - Index a single document

## MCP Tools

- `rag_retrieve_context` - Retrieve relevant context
- `rag_index_document` - Index a document
- `rag_get_metrics` - Get service metrics
- `rag_reindex_all` - Reindex all documents
- `kanban_rag_status` - Get RAG service status
