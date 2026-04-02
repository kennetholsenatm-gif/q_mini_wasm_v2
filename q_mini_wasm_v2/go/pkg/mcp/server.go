package mcp

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"strings"

	"github.com/q_mini_wasm_v2/gateway/pkg/rag"
)

// Server implements the MCP server
type Server struct {
	ragService *rag.Service
	reader     *bufio.Reader
	writer     io.Writer
}

// NewServer creates a new MCP server
func NewServer(ragService *rag.Service) *Server {
	return &Server{
		ragService: ragService,
		reader:     bufio.NewReader(os.Stdin),
		writer:     os.Stdout,
	}
}

// Run starts the MCP server loop
func (s *Server) Run(ctx context.Context) error {
	log.Println("MCP server started on stdio")

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
			// Read line from stdin
			line, err := s.reader.ReadString('\n')
			if err != nil {
				if err == io.EOF {
					return nil
				}
				return fmt.Errorf("failed to read input: %w", err)
			}

			// Parse request
			var req Request
			if err := json.Unmarshal([]byte(line), &req); err != nil {
				s.sendError(nil, -32700, "Parse error")
				continue
			}

			// Handle request
			resp := s.handleRequest(&req)
			if resp != nil {
				s.sendResponse(resp)
			}
		}
	}
}

// handleRequest processes a JSON-RPC request
func (s *Server) handleRequest(req *Request) *Response {
	switch req.Method {
	case "initialize":
		return s.handleInitialize(req)
	case "notifications/initialized":
		// No response needed for notifications
		return nil
	case "tools/list":
		return s.handleListTools(req)
	case "tools/call":
		return s.handleCallTool(req)
	case "ping":
		return s.handlePing(req)
	default:
		return s.errorResponse(req.ID, -32601, fmt.Sprintf("Method not found: %s", req.Method))
	}
}

// handleInitialize handles the initialize request
func (s *Server) handleInitialize(req *Request) *Response {
	result := InitializeResult{
		ProtocolVersion: "2024-11-05",
		Capabilities: ServerCapabilities{
			Tools: &ToolsCapability{},
		},
		ServerInfo: ServerInfo{
			Name:    "qminiwasm-rag",
			Version: "1.0.0",
		},
	}
	return s.successResponse(req.ID, result)
}

// handlePing handles ping requests
func (s *Server) handlePing(req *Request) *Response {
	return s.successResponse(req.ID, map[string]string{})
}

// handleListTools handles the tools/list request
func (s *Server) handleListTools(req *Request) *Response {
	tools := []Tool{
		{
			Name:        "rag_retrieve_context",
			Description: "Retrieve relevant context from project documentation and code",
			InputSchema: InputSchema{
				Type: "object",
				Properties: map[string]Property{
					"query": {
						Type:        "string",
						Description: "Query or code context to search for",
					},
					"max_tokens": {
						Type:        "integer",
						Description: "Maximum tokens to return (0 = auto-scale)",
						Default:     0,
					},
					"context_type": {
						Type:        "string",
						Description: "Type of context needed",
						Enum:        []string{"code", "documentation", "research", "api", "all"},
						Default:     "all",
					},
				},
				Required: []string{"query"},
			},
		},
		{
			Name:        "rag_index_document",
			Description: "Index a document into the RAG vector store",
			InputSchema: InputSchema{
				Type: "object",
				Properties: map[string]Property{
					"file_path": {
						Type:        "string",
						Description: "Path to the document to index",
					},
				},
				Required: []string{"file_path"},
			},
		},
		{
			Name:        "rag_get_metrics",
			Description: "Get RAG service performance metrics",
			InputSchema: InputSchema{
				Type: "object",
			},
		},
		{
			Name:        "rag_reindex_all",
			Description: "Trigger full re-indexing of all project documents",
			InputSchema: InputSchema{
				Type: "object",
				Properties: map[string]Property{
					"directory": {
						Type:        "string",
						Description: "Directory to reindex",
						Default:     "q_mini_wasm_v2",
					},
				},
			},
		},
		{
			Name:        "kanban_rag_status",
			Description: "Get RAG service status for Kanban pipeline",
			InputSchema: InputSchema{
				Type: "object",
				Properties: map[string]Property{
					"include_metrics": {
						Type:        "boolean",
						Description: "Include metrics in response",
						Default:     true,
					},
				},
			},
		},
	}

	return s.successResponse(req.ID, ListToolsResult{Tools: tools})
}

// handleCallTool handles the tools/call request
func (s *Server) handleCallTool(req *Request) *Response {
	var params CallToolParams
	if err := json.Unmarshal(req.Params, &params); err != nil {
		return s.errorResponse(req.ID, -32602, "Invalid params")
	}

	ctx := context.Background()

	switch params.Name {
	case "rag_retrieve_context":
		return s.callRetrieveContext(req.ID, params.Arguments)
	case "rag_index_document":
		return s.callIndexDocument(req.ID, params.Arguments)
	case "rag_get_metrics":
		return s.callGetMetrics(req.ID)
	case "rag_reindex_all":
		return s.callReindexAll(req.ID, params.Arguments)
	case "kanban_rag_status":
		return s.callKanbanStatus(req.ID, params.Arguments, ctx)
	default:
		return s.errorResponse(req.ID, -32601, fmt.Sprintf("Unknown tool: %s", params.Name))
	}
}

// callRetrieveContext handles rag_retrieve_context tool
func (s *Server) callRetrieveContext(id json.RawMessage, args json.RawMessage) *Response {
	var params struct {
		Query       string `json:"query"`
		MaxTokens   int    `json:"max_tokens"`
		ContextType string `json:"context_type"`
	}
	if err := json.Unmarshal(args, &params); err != nil {
		return s.errorResponse(id, -32602, "Invalid arguments")
	}

	if s.ragService == nil {
		return s.errorResponse(id, -32000, "RAG service not available")
	}

	result, err := s.ragService.RetrieveContext(context.Background(), params.Query, params.MaxTokens, params.ContextType)
	if err != nil {
		return s.errorResponse(id, -32000, fmt.Sprintf("RAG error: %v", err))
	}

	// Format result as text
	var text strings.Builder
	text.WriteString(fmt.Sprintf("Retrieved %d chunks (%d tokens):\n\n", len(result.Chunks), result.TotalTokens))
	for i, chunk := range result.Chunks {
		text.WriteString(fmt.Sprintf("--- Chunk %d (score: %.2f) ---\n", i+1, chunk.Score))
		text.WriteString(fmt.Sprintf("Source: %s (lines %d-%d)\n", chunk.SourceFile, chunk.StartLine, chunk.EndLine))
		text.WriteString(chunk.Content)
		text.WriteString("\n\n")
	}

	return s.successResponse(id, CallToolResult{
		Content: []Content{{Type: "text", Text: text.String()}},
	})
}

// callIndexDocument handles rag_index_document tool
func (s *Server) callIndexDocument(id json.RawMessage, args json.RawMessage) *Response {
	var params struct {
		FilePath string `json:"file_path"`
	}
	if err := json.Unmarshal(args, &params); err != nil {
		return s.errorResponse(id, -32602, "Invalid arguments")
	}

	if s.ragService == nil {
		return s.errorResponse(id, -32000, "RAG service not available")
	}

	if err := s.ragService.IndexDocument(context.Background(), params.FilePath); err != nil {
		return s.errorResponse(id, -32000, fmt.Sprintf("Indexing error: %v", err))
	}

	return s.successResponse(id, CallToolResult{
		Content: []Content{{Type: "text", Text: fmt.Sprintf("Successfully indexed: %s", params.FilePath)}},
	})
}

// callGetMetrics handles rag_get_metrics tool
func (s *Server) callGetMetrics(id json.RawMessage) *Response {
	if s.ragService == nil {
		return s.errorResponse(id, -32000, "RAG service not available")
	}

	metrics := s.ragService.GetMetrics()
	text := fmt.Sprintf(`RAG Service Metrics:
- Total Documents: %d
- Total Chunks: %d
- Total Queries: %d
- Avg Latency: %.2f ms
- Avg Tokens Saved: %.1f
- Cache Hit Rate: %.2f%%`,
		metrics.TotalDocuments,
		metrics.TotalChunks,
		metrics.TotalQueries,
		metrics.AvgLatencyMs,
		metrics.AvgTokensSaved,
		metrics.CacheHitRate*100,
	)

	return s.successResponse(id, CallToolResult{
		Content: []Content{{Type: "text", Text: text}},
	})
}

// callReindexAll handles rag_reindex_all tool
func (s *Server) callReindexAll(id json.RawMessage, args json.RawMessage) *Response {
	var params struct {
		Directory string `json:"directory"`
	}
	if err := json.Unmarshal(args, &params); err != nil {
		params.Directory = "q_mini_wasm_v2"
	}

	if s.ragService == nil {
		return s.errorResponse(id, -32000, "RAG service not available")
	}

	if err := s.ragService.IndexDirectory(context.Background(), params.Directory); err != nil {
		return s.errorResponse(id, -32000, fmt.Sprintf("Reindex error: %v", err))
	}

	metrics := s.ragService.GetMetrics()
	text := fmt.Sprintf("Reindexed directory: %s\nTotal documents: %d, Total chunks: %d",
		params.Directory, metrics.TotalDocuments, metrics.TotalChunks)

	return s.successResponse(id, CallToolResult{
		Content: []Content{{Type: "text", Text: text}},
	})
}

// callKanbanStatus handles kanban_rag_status tool
func (s *Server) callKanbanStatus(id json.RawMessage, args json.RawMessage, ctx context.Context) *Response {
	if s.ragService == nil {
		return s.successResponse(id, CallToolResult{
			Content: []Content{{Type: "text", Text: "RAG Service: Offline"}},
		})
	}

	metrics := s.ragService.GetMetrics()
	text := fmt.Sprintf(`RAG Service Status: Online
Documents Indexed: %d
Chunks Indexed: %d
Queries Served: %d
Avg Response Time: %.2f ms`,
		metrics.TotalDocuments,
		metrics.TotalChunks,
		metrics.TotalQueries,
		metrics.AvgLatencyMs,
	)

	return s.successResponse(id, CallToolResult{
		Content: []Content{{Type: "text", Text: text}},
	})
}

// sendResponse sends a JSON-RPC response
func (s *Server) sendResponse(resp *Response) {
	data, err := json.Marshal(resp)
	if err != nil {
		log.Printf("Failed to marshal response: %v", err)
		return
	}
	fmt.Fprintf(s.writer, "%s\n", data)
}

// sendError sends a JSON-RPC error response
func (s *Server) sendError(id json.RawMessage, code int, message string) {
	resp := &Response{
		JSONRPC: "2.0",
		ID:      id,
		Error:   &Error{Code: code, Message: message},
	}
	s.sendResponse(resp)
}

// successResponse creates a success response
func (s *Server) successResponse(id json.RawMessage, result interface{}) *Response {
	return &Response{
		JSONRPC: "2.0",
		ID:      id,
		Result:  result,
	}
}

// errorResponse creates an error response
func (s *Server) errorResponse(id json.RawMessage, code int, message string) *Response {
	return &Response{
		JSONRPC: "2.0",
		ID:      id,
		Error:   &Error{Code: code, Message: message},
	}
}