// Package grpc implements a simple gRPC server for RAG service
package grpc

import (
	"context"
	"fmt"
	"log"
	"net"

	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"

	"github.com/q_mini_wasm_v2/gateway/pkg/rag"
)

// Server implements a simple gRPC server
type Server struct {
	UnimplementedRAGServiceServer
	ragService *rag.Service
	port       int
	server     *grpc.Server
}

// UnimplementedRAGServiceServer is a placeholder for the gRPC service
type UnimplementedRAGServiceServer struct{}

// NewServer creates a new gRPC server
func NewServer(ragService *rag.Service, port int) *Server {
	return &Server{
		ragService: ragService,
		port:       port,
	}
}

// Start starts the gRPC server
func (s *Server) Start() error {
	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", s.port))
	if err != nil {
		return fmt.Errorf("failed to listen: %w", err)
	}

	s.server = grpc.NewServer()
	
	// Enable reflection for debugging
	reflection.Register(s.server)

	log.Printf("gRPC server listening on port %d", s.port)
	return s.server.Serve(lis)
}

// Stop stops the gRPC server
func (s *Server) Stop() {
	if s.server != nil {
		s.server.GracefulStop()
	}
}

// GetRAGService returns the RAG service instance
func (s *Server) GetRAGService() *rag.Service {
	return s.ragService
}

// RetrieveContext is a placeholder for the RetrieveContext RPC
func (s *Server) RetrieveContext(ctx context.Context, query string, maxTokens int, contextType string) (*rag.RetrieveResult, error) {
	if s.ragService == nil {
		return nil, fmt.Errorf("RAG service not available")
	}
	return s.ragService.RetrieveContext(ctx, query, maxTokens, contextType)
}