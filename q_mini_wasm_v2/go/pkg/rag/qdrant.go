// Package rag implements the RAG (Retrieval-Augmented Generation) service
// for token optimization in the q_mini_wasm_v2 project
package rag

import (
	"context"
	"fmt"
	"log"
	"time"

	"github.com/qdrant/go-client/qdrant"
)

// QdrantConfig holds Qdrant connection configuration
type QdrantConfig struct {
	Host           string        `json:"host"`
	Port           int           `json:"port"`
	CollectionName string        `json:"collection_name"`
	VectorSize     uint64        `json:"vector_size"`
	Timeout        time.Duration `json:"timeout"`
}

// QdrantClient wraps the Qdrant client with RAG-specific operations
type QdrantClient struct {
	client       *qdrant.Client
	config       QdrantConfig
	collection   string
}

// NewQdrantClient creates a new Qdrant client instance
func NewQdrantClient(config QdrantConfig) (*QdrantClient, error) {
	client, err := qdrant.NewClient(&qdrant.Config{
		Host: config.Host,
		Port: config.Port,
	})
	if err != nil {
		return nil, fmt.Errorf("failed to create Qdrant client: %w", err)
	}

	qc := &QdrantClient{
		client:     client,
		config:     config,
		collection: config.CollectionName,
	}

	// Ensure collection exists
	if err := qc.ensureCollection(context.Background()); err != nil {
		return nil, fmt.Errorf("failed to ensure collection: %w", err)
	}

	log.Printf("Qdrant client connected to %s:%d, collection: %s", 
		config.Host, config.Port, config.CollectionName)
	return qc, nil
}

// ensureCollection creates the collection if it doesn't exist
func (qc *QdrantClient) ensureCollection(ctx context.Context) error {
	collections, err := qc.client.ListCollections(ctx)
	if err != nil {
		return err
	}

	for _, col := range collections {
		if col == qc.collection {
			return nil // Collection exists
		}
	}

	// Create collection
	err = qc.client.CreateCollection(ctx, &qdrant.CreateCollection{
		CollectionName: qc.collection,
		VectorsConfig: &qdrant.VectorsConfig{
			Config: &qdrant.VectorsConfig_Params{
				Params: &qdrant.VectorParams{
					Size:     qc.config.VectorSize,
					Distance: qdrant.Distance_Cosine,
				},
			},
		},
	})
	if err != nil {
		return fmt.Errorf("failed to create collection: %w", err)
	}

	log.Printf("Created Qdrant collection: %s", qc.collection)
	return nil
}

// VectorPoint represents a point to be upserted
type VectorPoint struct {
	ID       string
	Vector   []float32
	Payload  map[string]interface{}
}

// UpsertPoints inserts or updates points in the collection
func (qc *QdrantClient) UpsertPoints(ctx context.Context, points []VectorPoint) error {
	qdrantPoints := make([]*qdrant.PointStruct, len(points))
	
	for i, p := range points {
		// Convert payload to Qdrant format
		payload := make(map[string]*qdrant.Value)
		for k, v := range p.Payload {
			val, err := qdrant.NewValue(v)
			if err == nil {
				payload[k] = val
			}
		}

		qdrantPoints[i] = &qdrant.PointStruct{
			Id:      qdrant.NewID(p.ID),
			Vectors: qdrant.NewVectors(p.Vector...),
			Payload: payload,
		}
	}

	_, err := qc.client.Upsert(ctx, &qdrant.UpsertPoints{
		CollectionName: qc.collection,
		Points:         qdrantPoints,
	})
	if err != nil {
		return fmt.Errorf("failed to upsert points: %w", err)
	}

	return nil
}

// SearchResult represents a search result from Qdrant
type SearchResult struct {
	ID         string
	Score      float32
	Content    string
	SourceFile string
	StartLine  int32
	EndLine    int32
	Tags       []string
	ChunkType  string
}

// Search performs a similarity search in the collection
func (qc *QdrantClient) Search(ctx context.Context, vector []float32, topK uint64, threshold float32) ([]SearchResult, error) {
	results, err := qc.client.Query(ctx, &qdrant.QueryPoints{
		CollectionName: qc.collection,
		Query:          qdrant.NewQuery(vector...),
		Limit:          &topK,
		ScoreThreshold: &threshold,
		WithPayload:    qdrant.NewWithPayload(true),
	})
	if err != nil {
		return nil, fmt.Errorf("failed to search: %w", err)
	}

	searchResults := make([]SearchResult, 0, len(results))
	for _, r := range results {
		sr := SearchResult{
			ID:    r.Id.GetUuid(),
			Score: r.Score,
		}

		// Extract payload
		if r.Payload != nil {
			if v, ok := r.Payload["content"]; ok {
				sr.Content = v.GetStringValue()
			}
			if v, ok := r.Payload["source_file"]; ok {
				sr.SourceFile = v.GetStringValue()
			}
			if v, ok := r.Payload["start_line"]; ok {
				sr.StartLine = int32(v.GetIntegerValue())
			}
			if v, ok := r.Payload["end_line"]; ok {
				sr.EndLine = int32(v.GetIntegerValue())
			}
			if v, ok := r.Payload["chunk_type"]; ok {
				sr.ChunkType = v.GetStringValue()
			}
			if v, ok := r.Payload["tags"]; ok {
				list := v.GetListValue()
				if list != nil {
					for _, tag := range list.Values {
						sr.Tags = append(sr.Tags, tag.GetStringValue())
					}
				}
			}
		}

		searchResults = append(searchResults, sr)
	}

	return searchResults, nil
}

// DeleteByFilter deletes points matching a filter
func (qc *QdrantClient) DeleteByFilter(ctx context.Context, filter map[string]interface{}) error {
	// Build Qdrant filter
	conditions := make([]*qdrant.Condition, 0, len(filter))
	for k, v := range filter {
		conditions = append(conditions, &qdrant.Condition{
			ConditionOneOf: &qdrant.Condition_Field{
				Field: &qdrant.FieldCondition{
					Key: k,
					Match: &qdrant.Match{
						MatchValue: &qdrant.Match_Keyword{
							Keyword: fmt.Sprintf("%v", v),
						},
					},
				},
			},
		})
	}

	_, err := qc.client.Delete(ctx, &qdrant.DeletePoints{
		CollectionName: qc.collection,
		Points: &qdrant.PointsSelector{
			PointsSelectorOneOf: &qdrant.PointsSelector_Filter{
				Filter: &qdrant.Filter{
					Must: conditions,
				},
			},
		},
	})
	if err != nil {
		return fmt.Errorf("failed to delete points: %w", err)
	}

	return nil
}

// GetCollectionInfo returns collection statistics
func (qc *QdrantClient) GetCollectionInfo(ctx context.Context) (*CollectionInfo, error) {
	info, err := qc.client.GetCollectionInfo(ctx, qc.collection)
	if err != nil {
		return nil, fmt.Errorf("failed to get collection info: %w", err)
	}

	var pointsCount uint64
	if info.PointsCount != nil {
		pointsCount = *info.PointsCount
	}

	var indexedCount uint64
	if info.IndexedVectorsCount != nil {
		indexedCount = *info.IndexedVectorsCount
	}

	return &CollectionInfo{
		Name:            qc.collection,
		PointsCount:     pointsCount,
		VectorsCount:    pointsCount,
		IndexedVectors:  indexedCount,
	}, nil
}

// CollectionInfo holds collection statistics
type CollectionInfo struct {
	Name           string
	PointsCount    uint64
	VectorsCount   uint64
	IndexedVectors uint64
}

// Close closes the Qdrant client connection
func (qc *QdrantClient) Close() error {
	return qc.client.Close()
}