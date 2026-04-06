package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// CodeAgent generates and refactors code
type CodeAgent struct {
	Name           string
	SupportedLangs []string
	CodeTemplates  map[string]string
	GeneratedCode  map[string]*GeneratedCodeResult
}

// GeneratedCodeResult represents generated code output
type GeneratedCodeResult struct {
	Language     string    `json:"language"`
	Code         string    `json:"code"`
	HeaderFile   string    `json:"header_file,omitempty"`
	BuildCommand string    `json:"build_command"`
	Dependencies []string  `json:"dependencies"`
	Timestamp    time.Time `json:"timestamp"`
	Description  string    `json:"description"`
}

// CodeRefactoring represents a refactoring operation
type CodeRefactoring struct {
	OriginalFile string   `json:"original_file"`
	Refactored   string   `json:"refactored_code"`
	Changes      []Change `json:"changes"`
	Improvement  string   `json:"improvement"`
}

// Change represents a code change
type Change struct {
	Type        string `json:"type"`
	Line        int    `json:"line"`
	OldCode     string `json:"old_code"`
	NewCode     string `json:"new_code"`
	Description string `json:"description"`
}

// NewCodeAgent creates a new CodeAgent
func NewCodeAgent() *CodeAgent {
	return &CodeAgent{
		Name:           "CodeAgent",
		SupportedLangs: []string{"C++", "Go", "R", "DLL"},
		CodeTemplates:  make(map[string]string),
		GeneratedCode:  make(map[string]*GeneratedCodeResult),
	}
}

// GenerateCode generates code for a specific task
func (c *CodeAgent) GenerateCode(language string, description string, requirements []string) (*GeneratedCodeResult, error) {
	supported := false
	for _, lang := range c.SupportedLangs {
		if lang == language {
			supported = true
			break
		}
	}
	if !supported {
		return nil, fmt.Errorf("language %s not supported. Supported: %s", language, strings.Join(c.SupportedLangs, ", "))
	}

	var code, header, buildCmd string
	var deps []string

	switch language {
	case "C++":
		code, header, buildCmd, deps = c.generateCPP(description, requirements)
	case "Go":
		code, header, buildCmd, deps = c.generateGo(description, requirements)
	case "R":
		code, header, buildCmd, deps = c.generateR(description, requirements)
	case "DLL":
		code, header, buildCmd, deps = c.generateDLL(description, requirements)
	}

	result := &GeneratedCodeResult{
		Language:     language,
		Code:         code,
		HeaderFile:   header,
		BuildCommand: buildCmd,
		Dependencies: deps,
		Timestamp:    time.Now(),
		Description:  description,
	}

	// Cache result
	cacheKey := fmt.Sprintf("%s_%d", language, time.Now().UnixNano())
	c.GeneratedCode[cacheKey] = result

	return result, nil
}

// generateCPP generates C++ code
func (c *CodeAgent) generateCPP(description string, requirements []string) (string, string, string, []string) {
	code := fmt.Sprintf(`// Generated C++ Code
// Description: %s
// Requirements: %s

#include <iostream>
#include <vector>
#include <memory>
#include <stdexcept>

namespace generated {

/**
 * @brief Generated class based on requirements
 */
class GeneratedComponent {
public:
    GeneratedComponent() = default;
    ~GeneratedComponent() = default;

    /**
     * @brief Process input data
     * @param input Input vector
     * @return Processed output
     */
    std::vector<double> process(const std::vector<double>& input) {
        std::vector<double> output;
        output.reserve(input.size());
        
        for (const auto& val : input) {
            // Apply scaling transformation to input values
            output.push_back(val * 2.0);
        }
        
        return output;
    }

    /**
     * @brief Validate input
     * @param input Input to validate
     * @return true if valid
     */
    bool validate(const std::vector<double>& input) {
        return !input.empty();
    }
};

} // namespace generated

int main() {
    generated::GeneratedComponent component;
    
    std::vector<double> input = {1.0, 2.0, 3.0, 4.0, 5.0};
    auto output = component.process(input);
    
    std::cout << "Output: ";
    for (const auto& val : output) {
        std::cout << val << " ";
    }
    std::cout << std::endl;
    
    return 0;
}
`, description, strings.Join(requirements, ", "))

	header := `#pragma once

#include <vector>

namespace generated {

class GeneratedComponent {
public:
    GeneratedComponent();
    ~GeneratedComponent();
    
    std::vector<double> process(const std::vector<double>& input);
    bool validate(const std::vector<double>& input);
};

} // namespace generated
`

	buildCmd := "g++ -std=c++17 -O2 -o output main.cpp"
	deps := []string{"C++17 or later"}

	return code, header, buildCmd, deps
}

// generateGo generates Go code
func (c *CodeAgent) generateGo(description string, requirements []string) (string, string, string, []string) {
	code := fmt.Sprintf(`// Generated Go Code
// Description: %s
// Requirements: %s

package main

import (
	"fmt"
)

// GeneratedComponent represents the main component
type GeneratedComponent struct {
	name string
}

// NewGeneratedComponent creates a new component
func NewGeneratedComponent() *GeneratedComponent {
	return &GeneratedComponent{name: "generated"}
}

// Process handles input processing
func (gc *GeneratedComponent) Process(input []float64) []float64 {
	output := make([]float64, len(input))
	for i, val := range input {
		// Apply scaling transformation to input values
		output[i] = val * 2.0
	}
	return output
}

// Validate checks input validity
func (gc *GeneratedComponent) Validate(input []float64) bool {
	return len(input) > 0
}

func main() {
	component := NewGeneratedComponent()
	
	input := []float64{1.0, 2.0, 3.0, 4.0, 5.0}
	output := component.Process(input)
	
	fmt.Printf("Output: %v\n", output)
}
`, description, strings.Join(requirements, ", "))

	buildCmd := "go build -o output main.go"
	deps := []string{"Go 1.21 or later"}

	return code, "", buildCmd, deps
}

// generateR generates R code
func (c *CodeAgent) generateR(description string, requirements []string) (string, string, string, []string) {
	code := fmt.Sprintf(`# Generated R Code
# Description: %s
# Requirements: %s

#' Generated Component
#' @description Main processing component
GeneratedComponent <- list(
  name = "generated"
)

#' Process input data
#' @param input Numeric vector input
#' @return Processed numeric vector
process <- function(input) {
  if (length(input) == 0) {
    stop("Input cannot be empty")
  }
  # Apply scaling transformation to input values
  output <- input * 2.0
  return(output)
}

#' Validate input
#' @param input Numeric vector to validate
#' @return Logical indicating validity
validate <- function(input) {
  return(length(input) > 0 && is.numeric(input))
}

# Main execution
input <- c(1.0, 2.0, 3.0, 4.0, 5.0)
output <- process(input)

cat("Output:", paste(output, collapse = " "), "\n")
`, description, strings.Join(requirements, ", "))

	buildCmd := "Rscript main.R"
	deps := []string{"R 4.0 or later"}

	return code, "", buildCmd, deps
}

// generateDLL generates DLL (C++) code
func (c *CodeAgent) generateDLL(description string, requirements []string) (string, string, string, []string) {
	code := fmt.Sprintf(`// Generated DLL Code
// Description: %s
// Requirements: %s

#include <windows.h>
#include <vector>
#include <iostream>

// DLL Export macro
#ifdef Q_MINI_WASM_V2_EXPORTS
    #define DLL_API __declspec(dllexport)
#else
    #define DLL_API __declspec(dllimport)
#endif

extern "C" {

/**
 * @brief Initialize the component
 * @return Handle to the component, or NULL on failure
 */
DLL_API void* component_create() {
    try {
        auto component = new std::vector<double>();
        return static_cast<void*>(component);
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Destroy the component
 * @param handle Component handle
 */
DLL_API void component_destroy(void* handle) {
    if (handle != nullptr) {
        delete static_cast<std::vector<double>*>(handle);
    }
}

/**
 * @brief Process input array
 * @param handle Component handle
 * @param input Input array
 * @param input_size Size of input array
 * @param output Output array (caller-allocated)
 * @param output_size Size of output array
 * @return Number of elements written, or 0 on error
 */
DLL_API size_t component_process(
    void* handle,
    const double* input,
    size_t input_size,
    double* output,
    size_t output_size
) {
    if (handle == nullptr || input == nullptr || output == nullptr) {
        return 0;
    }
    
    size_t count = std::min(input_size, output_size);
    for (size_t i = 0; i < count; ++i) {
        // Apply scaling transformation to input values
        output[i] = input[i] * 2.0;
    }
    
    return count;
}

/**
 * @brief Get version string
 * @return Version string
 */
DLL_API const char* component_version() {
    return "1.0.0";
}

} // extern "C"
`, description, strings.Join(requirements, ", "))

	header := `#pragma once

#ifdef _WIN32
    #ifdef Q_MINI_WASM_V2_EXPORTS
        #define DLL_API __declspec(dllexport)
    #else
        #define DLL_API __declspec(dllimport)
    #endif
#else
    #define DLL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

DLL_API void* component_create();
DLL_API void component_destroy(void* handle);
DLL_API size_t component_process(void* handle, const double* input, size_t input_size, double* output, size_t output_size);
DLL_API const char* component_version();

#ifdef __cplusplus
}
#endif
`

	buildCmd := "cl /LD /Fe:component.dll main.cpp"
	deps := []string{"Windows SDK", "C++ compiler"}

	return code, header, buildCmd, deps
}

// RefactorCode performs code refactoring
func (c *CodeAgent) RefactorCode(filePath string, refactorType string) (*CodeRefactoring, error) {
	content, err := os.ReadFile(filePath)
	if err != nil {
		return nil, fmt.Errorf("failed to read file: %w", err)
	}

	refactoring := &CodeRefactoring{
		OriginalFile: filePath,
		Refactored:   string(content),
		Changes:      []Change{},
		Improvement:  "",
	}

	// Apply refactoring based on type
	switch refactorType {
	case "optimize_loops":
		c.optimizeLoops(refactoring)
	case "add_error_handling":
		c.addErrorHandling(refactoring)
	case "simplify_conditionals":
		c.simplifyConditionals(refactoring)
	case "extract_functions":
		c.extractFunctions(refactoring)
	default:
		return nil, fmt.Errorf("unknown refactoring type: %s", refactorType)
	}

	return refactoring, nil
}

// optimizeLoops optimizes loop constructs
func (c *CodeAgent) optimizeLoops(r *CodeRefactoring) {
	lines := strings.Split(r.Refactored, "\n")
	for i, line := range lines {
		if strings.Contains(line, "for") && strings.Contains(line, "size_t i") {
			r.Changes = append(r.Changes, Change{
				Type:        "optimization",
				Line:        i + 1,
				OldCode:     line,
				NewCode:     "// Consider range-based for loop",
				Description: "Loop optimization suggestion",
			})
		}
	}
	r.Improvement = "Loop optimizations suggested"
}

// addErrorHandling adds error handling to code
func (c *CodeAgent) addErrorHandling(r *CodeRefactoring) {
	lines := strings.Split(r.Refactored, "\n")
	for i, line := range lines {
		if strings.Contains(line, "new ") && !strings.Contains(line, "try") {
			r.Changes = append(r.Changes, Change{
				Type:        "error_handling",
				Line:        i + 1,
				OldCode:     line,
				NewCode:     "// Add try-catch or error checking",
				Description: "Add error handling for allocation",
			})
		}
	}
	r.Improvement = "Error handling improvements suggested"
}

// simplifyConditionals simplifies conditional expressions
func (c *CodeAgent) simplifyConditionals(r *CodeRefactoring) {
	// Analyze code for complex conditionals that can be simplified
	content := r.Refactored
	lines := strings.Split(content, "\n")
	improvements := 0

	for i, line := range lines {
		line = strings.TrimSpace(line)

		// Pattern 1: Nested if statements that could be combined
		if strings.Contains(line, "if ") && strings.Count(line, "&&") == 0 && strings.Count(line, "||") == 0 {
			// Check for consecutive if statements with same body pattern
			if i+1 < len(lines) && strings.Contains(lines[i+1], "if ") {
				r.Changes = append(r.Changes, Change{
					Type:        "simplify",
					Line:        i + 1,
					OldCode:     line,
					NewCode:     "// Consider combining with next if using &&",
					Description: "Nested if statements can be combined",
				})
				improvements++
			}
		}

		// Pattern 2: Boolean comparisons that can be simplified
		if strings.Contains(line, "== true") || strings.Contains(line, "== false") {
			r.Changes = append(r.Changes, Change{
				Type:        "simplify",
				Line:        i + 1,
				OldCode:     line,
				NewCode:     strings.Replace(strings.Replace(line, "== true", "", -1), "== false", "!", -1),
				Description: "Remove redundant boolean comparison",
			})
			improvements++
		}
	}

	if improvements > 0 {
		r.Improvement = fmt.Sprintf("Found %d conditional simplification opportunities", improvements)
	} else {
		r.Improvement = "No conditional simplification opportunities found"
	}
}

// extractFunctions extracts code blocks into separate functions
func (c *CodeAgent) extractFunctions(r *CodeRefactoring) {
	lines := strings.Split(r.Refactored, "\n")
	extractionOpportunities := 0

	// Track function boundaries and identify extractable blocks
	inFunction := false
	braceDepth := 0
	blockStart := -1
	blockLines := []string{}

	for i, line := range lines {
		trimmed := strings.TrimSpace(line)

		// Detect function start
		if !inFunction && (strings.Contains(trimmed, "func ") ||
			(strings.Contains(trimmed, "{") && i > 0 && strings.Contains(lines[i-1], "func "))) {
			inFunction = true
			braceDepth = 0
		}

		if inFunction {
			// Track brace depth
			braceDepth += strings.Count(line, "{")
			braceDepth -= strings.Count(line, "}")

			// Detect potential extraction: code blocks with comments indicating operations
			if strings.Contains(trimmed, "// ") && len(trimmed) > 20 {
				if blockStart == -1 {
					blockStart = i
					blockLines = []string{}
				}
				blockLines = append(blockLines, line)
			}

			// End of a potential block (empty line or closing brace)
			if blockStart != -1 && (trimmed == "" || trimmed == "}") {
				if len(blockLines) >= 3 {
					// This is a good candidate for extraction
					comment := strings.TrimPrefix(strings.TrimSpace(blockLines[0]), "// ")
					funcName := generateFunctionName(comment)

					r.Changes = append(r.Changes, Change{
						Type:        "extract",
						Line:        blockStart + 1,
						OldCode:     strings.Join(blockLines, "\n"),
						NewCode:     fmt.Sprintf("%s() // Extracted from comment: %s", funcName, comment),
						Description: fmt.Sprintf("Extract lines %d-%d into function '%s'", blockStart+1, i, funcName),
					})
					extractionOpportunities++
				}
				blockStart = -1
				blockLines = []string{}
			}

			// End of function
			if braceDepth <= 0 && inFunction {
				inFunction = false
			}
		}
	}

	// Look for repeated patterns (duplicate code detection)
	duplicates := findDuplicatePatterns(lines)
	for _, dup := range duplicates {
		r.Changes = append(r.Changes, Change{
			Type:        "extract",
			Line:        dup.StartLine,
			OldCode:     dup.Code,
			NewCode:     fmt.Sprintf("%s() // Extracted common pattern", dup.SuggestedName),
			Description: fmt.Sprintf("Extract duplicate code at line %d into function '%s'", dup.StartLine, dup.SuggestedName),
		})
		extractionOpportunities++
	}

	if extractionOpportunities > 0 {
		r.Improvement = fmt.Sprintf("Found %d function extraction opportunities", extractionOpportunities)
	} else {
		r.Improvement = "No function extraction opportunities found"
	}
}

// generateFunctionName creates a function name from a comment
type DuplicatePattern struct {
	StartLine       int
	Code            string
	SuggestedName   string
	OccurrenceCount int
}

func generateFunctionName(comment string) string {
	// Extract meaningful words from comment
	words := strings.Fields(strings.ToLower(comment))
	name := "perform"
	for _, w := range words {
		if len(w) > 3 && !strings.Contains(w, "the") && !strings.Contains(w, "and") {
			name = strings.Title(w)
			break
		}
	}
	return name + "Operation"
}

// findDuplicatePatterns detects repeated code blocks
func findDuplicatePatterns(lines []string) []DuplicatePattern {
	duplicates := []DuplicatePattern{}
	seen := make(map[string]int)

	// Look for 3+ line sequences that repeat
	for i := 0; i < len(lines)-2; i++ {
		block := strings.TrimSpace(lines[i]) + "|" +
			strings.TrimSpace(lines[i+1]) + "|" +
			strings.TrimSpace(lines[i+2])

		if prevIdx, found := seen[block]; found && prevIdx != i {
			// Found a duplicate
			duplicates = append(duplicates, DuplicatePattern{
				StartLine:       i + 1,
				Code:            strings.Join(lines[i:i+3], "\n"),
				SuggestedName:   "commonOperation",
				OccurrenceCount: 2,
			})
		}
		seen[block] = i
	}

	return duplicates
}

// GetGeneratedCodeSummary returns summary of generated code
func (c *CodeAgent) GetGeneratedCodeSummary() map[string]interface{} {
	totalGenerated := len(c.GeneratedCode)
	languages := make(map[string]int)

	for _, result := range c.GeneratedCode {
		languages[result.Language]++
	}

	return map[string]interface{}{
		"total_generated": totalGenerated,
		"by_language":     languages,
		"supported_langs": c.SupportedLangs,
	}
}

func main() {
	agent := NewCodeAgent()

	if len(os.Args) > 1 {
		command := os.Args[1]

		switch command {
		case "generate":
			if len(os.Args) < 4 {
				fmt.Println("Usage: code-agent generate <language> <description>")
				os.Exit(1)
			}
			language := os.Args[2]
			description := os.Args[3]
			requirements := []string{}
			if len(os.Args) > 4 {
				requirements = os.Args[4:]
			}

			result, err := agent.GenerateCode(language, description, requirements)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(result)

		case "refactor":
			if len(os.Args) < 4 {
				fmt.Println("Usage: code-agent refactor <file> <refactor-type>")
				os.Exit(1)
			}
			filePath := os.Args[2]
			refactorType := os.Args[3]

			result, err := agent.RefactorCode(filePath, refactorType)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(result)

		case "summary":
			summary := agent.GetGeneratedCodeSummary()
			json.NewEncoder(os.Stdout).Encode(summary)

		default:
			fmt.Println("Code Agent - Code Generation and Refactoring")
			fmt.Println("Supported languages:", strings.Join(agent.SupportedLangs, ", "))
			fmt.Println("\nCommands:")
			fmt.Println("  generate <lang> <desc> [req...]  - Generate code")
			fmt.Println("  refactor <file> <type>           - Refactor code")
			fmt.Println("  summary                          - Get generation summary")
			fmt.Println("\nRefactor types: optimize_loops, add_error_handling, simplify_conditionals, extract_functions")
		}
	} else {
		fmt.Println("Code Agent - Code Generation and Refactoring")
		fmt.Println("Supported languages:", strings.Join(agent.SupportedLangs, ", "))
		fmt.Println("\nUsage: code-agent <command> [args]")
	}
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

var _ = filepath.Join // Ensure filepath is used
