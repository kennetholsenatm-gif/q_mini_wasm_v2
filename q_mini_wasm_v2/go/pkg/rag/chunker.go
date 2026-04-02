package rag

import (
	"bufio"
	"crypto/sha256"
	"fmt"
	"path/filepath"
	"regexp"
	"strings"
)

// ChunkConfig holds chunking configuration
type ChunkConfig struct {
	MaxChunkSize     int     `json:"max_chunk_size"`     // Maximum tokens per chunk
	OverlapSize      int     `json:"overlap_size"`        // Overlap between chunks
	MinChunkSize     int     `json:"min_chunk_size"`      // Minimum chunk size
	RespectCodeBlocks bool   `json:"respect_code_blocks"` // Keep code blocks intact
}

// Chunk represents a document chunk
type Chunk struct {
	ID         string   `json:"id"`
	Content    string   `json:"content"`
	SourceFile string   `json:"source_file"`
	StartLine  int      `json:"start_line"`
	EndLine    int      `json:"end_line"`
	Tags       []string `json:"tags"`
	ChunkType  string   `json:"chunk_type"`
	TokenCount int      `json:"token_count"`
}

// Chunker handles document chunking with code-aware strategies
type Chunker struct {
	config ChunkConfig
}

// NewChunker creates a new chunker instance
func NewChunker(config ChunkConfig) *Chunker {
	if config.MaxChunkSize == 0 {
		config.MaxChunkSize = 512 // Default 512 tokens
	}
	if config.OverlapSize == 0 {
		config.OverlapSize = 50 // Default 50 token overlap
	}
	if config.MinChunkSize == 0 {
		config.MinChunkSize = 50
	}

	return &Chunker{config: config}
}

// ChunkDocument splits a document into chunks based on file type
func (c *Chunker) ChunkDocument(filePath, content string, tags []string) ([]Chunk, error) {
	ext := strings.ToLower(filepath.Ext(filePath))
	
	switch ext {
	case ".cpp", ".hpp", ".c", ".h":
		return c.chunkCode(filePath, content, tags, "cpp")
	case ".go":
		return c.chunkCode(filePath, content, tags, "go")
	case ".rs":
		return c.chunkCode(filePath, content, tags, "rust")
	case ".py":
		return c.chunkCode(filePath, content, tags, "python")
	case ".md":
		return c.chunkMarkdown(filePath, content, tags)
	case ".txt":
		return c.chunkText(filePath, content, tags)
	default:
		return c.chunkText(filePath, content, tags)
	}
}

// chunkCode handles code-aware chunking
func (c *Chunker) chunkCode(filePath, content string, tags []string, lang string) ([]Chunk, error) {
	var chunks []Chunk
	lines := strings.Split(content, "\n")
	
	// Regex patterns for different languages
	var (
		funcPattern *regexp.Regexp
		classPattern *regexp.Regexp
		commentStart *regexp.Regexp
		commentEnd   *regexp.Regexp
	)

	switch lang {
	case "cpp", "c":
		funcPattern = regexp.MustCompile(`^\s*(?:static\s+)?(?:inline\s+)?(?:virtual\s+)?(?:explicit\s+)?(?:friend\s+)?(?:constexpr\s+)?[\w:*&<>\s]+\s+(\w+)\s*\(`)
		classPattern = regexp.MustCompile(`^\s*(?:class|struct|namespace)\s+(\w+)`)
		commentStart = regexp.MustCompile(`/^\s*\/\*/`)
		commentEnd = regexp.MustCompile(`/^\s*\*\//`)
	case "go":
		funcPattern = regexp.MustCompile(`^\s*func\s+(?:\([^)]+\)\s+)?(\w+)\s*\(`)
		classPattern = regexp.MustCompile(`^\s*type\s+(\w+)\s+(?:struct|interface)`)
	case "rust":
		funcPattern = regexp.MustCompile(`^\s*(?:pub\s+)?(?:async\s+)?fn\s+(\w+)\s*(?:<[^>]*>)?\s*\(`)
		classPattern = regexp.MustCompile(`^\s*(?:pub\s+)?(?:struct|enum|trait|impl)\s+(\w+)`)
	case "python":
		funcPattern = regexp.MustCompile(`^\s*def\s+(\w+)\s*\(`)
		classPattern = regexp.MustCompile(`^\s*class\s+(\w+)\s*(?:\([^)]*\))?:`)
	}

	// Track current chunk
	var currentChunk strings.Builder
	currentStart := 1
	inBlockComment := false
	braceDepth := 0

	for i, line := range lines {
		lineNum := i + 1

		// Track block comments
		if commentStart != nil && commentStart.MatchString(line) {
			inBlockComment = true
		}
		if commentEnd != nil && commentEnd.MatchString(line) {
			inBlockComment = false
		}

		// Track brace depth for code blocks
		braceDepth += strings.Count(line, "{") - strings.Count(line, "}")

		// Check for function/class boundaries
		isBoundary := false
		if !inBlockComment && c.config.RespectCodeBlocks {
			if funcPattern != nil && funcPattern.MatchString(line) {
				isBoundary = true
			}
			if classPattern != nil && classPattern.MatchString(line) {
				isBoundary = true
			}
		}

		// Estimate tokens (rough: 1 token ≈ 4 chars)
		currentTokens := c.estimateTokens(currentChunk.String())
		lineTokens := c.estimateTokens(line)

		// Decide whether to start a new chunk
		shouldSplit := false
		if isBoundary && currentTokens > c.config.MinChunkSize {
			shouldSplit = true
		}
		if currentTokens+lineTokens > c.config.MaxChunkSize && braceDepth == 0 {
			shouldSplit = true
		}

		if shouldSplit && currentChunk.Len() > 0 {
			// Create chunk
			chunk := Chunk{
				ID:         c.generateID(filePath, currentStart, lineNum-1),
				Content:    currentChunk.String(),
				SourceFile: filePath,
				StartLine:  currentStart,
				EndLine:    lineNum - 1,
				Tags:       tags,
				ChunkType:  "code",
				TokenCount: currentTokens,
			}
			chunks = append(chunks, chunk)

			// Start new chunk with overlap
			currentChunk.Reset()
			currentStart = lineNum - c.config.OverlapSize
			if currentStart < 1 {
				currentStart = 1
			}

			// Add overlap lines
			overlapStart := currentStart - 1
			if overlapStart >= 0 && overlapStart < len(lines) {
				for j := overlapStart; j < i && j < overlapStart+c.config.OverlapSize; j++ {
					if j >= 0 && j < len(lines) {
						currentChunk.WriteString(lines[j])
						currentChunk.WriteString("\n")
					}
				}
			}
		}

		// Add current line
		currentChunk.WriteString(line)
		if i < len(lines)-1 {
			currentChunk.WriteString("\n")
		}
	}

	// Add final chunk
	if currentChunk.Len() > 0 {
		chunk := Chunk{
			ID:         c.generateID(filePath, currentStart, len(lines)),
			Content:    currentChunk.String(),
			SourceFile: filePath,
			StartLine:  currentStart,
			EndLine:    len(lines),
			Tags:       tags,
			ChunkType:  "code",
			TokenCount: c.estimateTokens(currentChunk.String()),
		}
		chunks = append(chunks, chunk)
	}

	return chunks, nil
}

// chunkMarkdown handles markdown-aware chunking
func (c *Chunker) chunkMarkdown(filePath, content string, tags []string) ([]Chunk, error) {
	var chunks []Chunk
	scanner := bufio.NewScanner(strings.NewReader(content))
	
	var currentChunk strings.Builder
	currentStart := 1
	lineNum := 0
	currentSection := ""

	for scanner.Scan() {
		lineNum++
		line := scanner.Text()

		// Detect markdown headers
		if strings.HasPrefix(line, "#") {
			// Extract header level and text
			headerMatch := regexp.MustCompile(`^(#+)\s+(.+)`).FindStringSubmatch(line)
			if len(headerMatch) > 2 {
				newSection := headerMatch[2]
				
				// Start new chunk at major headers
				if len(headerMatch[1]) <= 2 && currentChunk.Len() > 0 {
					chunk := Chunk{
						ID:         c.generateID(filePath, currentStart, lineNum-1),
						Content:    currentChunk.String(),
						SourceFile: filePath,
						StartLine:  currentStart,
						EndLine:    lineNum - 1,
						Tags:       append(tags, "section:"+currentSection),
						ChunkType:  "documentation",
						TokenCount: c.estimateTokens(currentChunk.String()),
					}
					chunks = append(chunks, chunk)
					
					currentChunk.Reset()
					currentStart = lineNum
				}
				currentSection = newSection
			}
		}

		currentChunk.WriteString(line)
		currentChunk.WriteString("\n")

		// Split on size limit
		if c.estimateTokens(currentChunk.String()) > c.config.MaxChunkSize {
			chunk := Chunk{
				ID:         c.generateID(filePath, currentStart, lineNum),
				Content:    currentChunk.String(),
				SourceFile: filePath,
				StartLine:  currentStart,
				EndLine:    lineNum,
				Tags:       append(tags, "section:"+currentSection),
				ChunkType:  "documentation",
				TokenCount: c.estimateTokens(currentChunk.String()),
			}
			chunks = append(chunks, chunk)
			
			currentChunk.Reset()
			currentStart = lineNum + 1
		}
	}

	// Add final chunk
	if currentChunk.Len() > 0 {
		chunk := Chunk{
			ID:         c.generateID(filePath, currentStart, lineNum),
			Content:    currentChunk.String(),
			SourceFile: filePath,
			StartLine:  currentStart,
			EndLine:    lineNum,
			Tags:       append(tags, "section:"+currentSection),
			ChunkType:  "documentation",
			TokenCount: c.estimateTokens(currentChunk.String()),
		}
		chunks = append(chunks, chunk)
	}

	return chunks, nil
}

// chunkText handles plain text chunking
func (c *Chunker) chunkText(filePath, content string, tags []string) ([]Chunk, error) {
	var chunks []Chunk
	scanner := bufio.NewScanner(strings.NewReader(content))
	
	var currentChunk strings.Builder
	currentStart := 1
	lineNum := 0

	for scanner.Scan() {
		lineNum++
		line := scanner.Text()

		currentChunk.WriteString(line)
		currentChunk.WriteString("\n")

		// Split on size limit
		if c.estimateTokens(currentChunk.String()) > c.config.MaxChunkSize {
			chunk := Chunk{
				ID:         c.generateID(filePath, currentStart, lineNum),
				Content:    currentChunk.String(),
				SourceFile: filePath,
				StartLine:  currentStart,
				EndLine:    lineNum,
				Tags:       tags,
				ChunkType:  "text",
				TokenCount: c.estimateTokens(currentChunk.String()),
			}
			chunks = append(chunks, chunk)
			
			// Overlap
			currentChunk.Reset()
			currentStart = lineNum - c.config.OverlapSize
			if currentStart < 1 {
				currentStart = 1
			}
		}
	}

	// Add final chunk
	if currentChunk.Len() > 0 {
		chunk := Chunk{
			ID:         c.generateID(filePath, currentStart, lineNum),
			Content:    currentChunk.String(),
			SourceFile: filePath,
			StartLine:  currentStart,
			EndLine:    lineNum,
			Tags:       tags,
			ChunkType:  "text",
			TokenCount: c.estimateTokens(currentChunk.String()),
		}
		chunks = append(chunks, chunk)
	}

	return chunks, nil
}

// estimateTokens estimates token count (rough: 1 token ≈ 4 chars)
func (c *Chunker) estimateTokens(text string) int {
	return len(text) / 4
}

// generateID creates a unique ID for a chunk (UUID format)
func (c *Chunker) generateID(filePath string, startLine, endLine int) string {
	hash := sha256.Sum256([]byte(fmt.Sprintf("%s:%d:%d", filePath, startLine, endLine)))
	// Convert 16 bytes to UUID format (8-4-4-4-12)
	return fmt.Sprintf("%x-%x-%x-%x-%x",
		hash[0:4],
		hash[4:6],
		hash[6:8],
		hash[8:10],
		hash[10:16])
}
