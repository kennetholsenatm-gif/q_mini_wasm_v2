// Package main implements the q_mini_wasm_v2 MCP Gateway Host
// Provides MCP server over stdio for agents to interact with RAG and core operations
package main

import (
	"context"
	"flag"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/q_mini_wasm_v2/gateway/pkg/mcp"
	"github.com/q_mini_wasm_v2/gateway/pkg/rag"
)

// GatewayConfig holds the gateway service configuration
type GatewayConfig struct {
	EngineLibPath  string `json:"engine_lib_path"`
	QdrantHost     string `json:"qdrant_host"`
	QdrantPort     int    `json:"qdrant_port"`
	ProjectRoot    string `json:"project_root"`
}

func main() {
	// Parse command line flags
	mcpMode := flag.Bool("mcp", true, "Run in MCP server mode (stdio)")
	flag.Parse()

	config := GatewayConfig{
		EngineLibPath:  "./libq_mini_wasm_v2_core.so",
		QdrantHost:     "localhost",
		QdrantPort:     6334,
		ProjectRoot:    ".",
	}

	// Initialize RAG service with improved configuration
	ragConfig := rag.ServiceConfig{
		Qdrant: rag.QdrantConfig{
			Host:           config.QdrantHost,
			Port:           config.QdrantPort,
			CollectionName: "qminiwasm_docs",
			VectorSize:     384,
		},
		Chunker: rag.ChunkConfig{
			MaxChunkSize:      512,
			OverlapSize:       50,
			MinChunkSize:      50,
			RespectCodeBlocks: true,
		},
		Scaler: rag.TokenScalerConfig{
			MinTokens:        256,
			MaxTokens:        8192,
			DefaultTokens:    2048,
			ScalingFactor:    1.5,
			ComplexityWeight: 0.6,
			ContextWeight:    0.4,
		},
		HybridSearch: rag.HybridSearchConfig{
			SemanticWeight:  0.7,
			KeywordWeight:   0.3,
			RerankTopK:      20,
			MinScore:        0.3,
		},
		Cache: rag.CacheConfig{
			MaxSize:        1000,
			TTL:            5 * time.Minute,
			EvictionPolicy: "lru",
		},
		ProjectRoot:   config.ProjectRoot,
		AutoIndex:     true,
		EmbeddingType: "placeholder", // Can be changed to "tfidf" or "composite"
	}

	ragService, err := rag.NewService(ragConfig)
	if err != nil {
		log.Printf("Warning: Failed to initialize RAG service: %v", err)
		log.Println("Continuing without RAG service...")
		ragService = nil
	} else {
		// Start RAG service
		ctx := context.Background()
		if err := ragService.Start(ctx); err != nil {
			log.Printf("Warning: Failed to start RAG service: %v", err)
			ragService = nil
		} else {
			log.Println("RAG service started successfully")
		}
	}

	// Graceful shutdown handling for context cancellation
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		<-sigChan
		log.Println("Shutting down MCP Gateway...")
		cancel()
	}()

	if *mcpMode {
		log.Println("Starting in MCP server mode (stdio)")
		mcpServer := mcp.NewServer(ragService)
		if err := mcpServer.Run(ctx); err != nil && err != context.Canceled {
			log.Fatalf("MCP server failed: %v", err)
		}
	} else {
		log.Println("Gateway must be run in MCP mode.")
	}

	// Stop RAG service
	if ragService != nil {
		ragService.Stop()
	}

	log.Println("Gateway stopped")
}
