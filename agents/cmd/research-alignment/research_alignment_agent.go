package agents

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"
)

// AlignmentStatus represents the status of code alignment with research
type AlignmentStatus string

const (
	Aligned          AlignmentStatus = "aligned"
	PartiallyAligned AlignmentStatus = "partially_aligned"
	NotAligned       AlignmentStatus = "not_aligned"
	Unknown          AlignmentStatus = "unknown"
)

// ResearchExtract contains extracted information from research documents
type ResearchExtract struct {
	Title           string                 `json:"title"`
	Authors         []string               `json:"authors"`
	Algorithms      []Algorithm            `json:"algorithms"`
	Methods         []Method               `json:"methods"`
	KeyFormulas     []string               `json:"key_formulas"`
	Pseudocode      []string               `json:"pseudocode"`
	ExpectedResults map[string]interface{} `json:"expected_results"`
	Timestamp       time.Time              `json:"timestamp"`
}

// Algorithm represents an algorithm extracted from research
type Algorithm struct {
	Name        string            `json:"name"`
	Description string            `json:"description"`
	Steps       []string          `json:"steps"`
	Complexity  string            `json:"complexity"`
	Parameters  map[string]string `json:"parameters"`
}

// Method represents a method extracted from research
type Method struct {
	Name        string   `json:"name"`
	Description string   `json:"description"`
	Inputs      []string `json:"inputs"`
	Outputs     []string `json:"outputs"`
}

// CodeAnalysis represents analysis of existing code
type CodeAnalysis struct {
	FilePath              string                 `json:"file_path"`
	Language              string                 `json:"language"`
	Functions             []Function             `json:"functions"`
	Classes               []Class                `json:"classes"`
	AlgorithmsImplemented []string               `json:"algorithms_implemented"`
	Dependencies          []string               `json:"dependencies"`
	ComplexityMetrics     map[string]interface{} `json:"complexity_metrics"`
}

// Function represents a function in code
type Function struct {
	Name        string   `json:"name"`
	Signature   string   `json:"signature"`
	Description string   `json:"description"`
	Parameters  []string `json:"parameters"`
	ReturnType  string   `json:"return_type"`
}

// Class represents a class in code
type Class struct {
	Name        string   `json:"name"`
	Methods     []string `json:"methods"`
	Members     []string `json:"members"`
	Description string   `json:"description"`
}

// AlignmentResult represents the result of alignment check
type AlignmentResult struct {
	OverallStatus       AlignmentStatus      `json:"overall_status"`
	AlignmentScore      float64              `json:"alignment_score"`
	AlignedComponents   []AlignedComponent   `json:"aligned_components"`
	MissingComponents   []MissingComponent   `json:"missing_components"`
	DivergentComponents []DivergentComponent `json:"divergent_components"`
	Suggestions         []Suggestion         `json:"suggestions"`
}

// AlignedComponent represents a component that aligns with research
type AlignedComponent struct {
	ResearchComponent string `json:"research_component"`
	CodeComponent     string `json:"code_component"`
	Quality           string `json:"quality"`
}

// MissingComponent represents a missing component
type MissingComponent struct {
	Component   string `json:"component"`
	Importance  string `json:"importance"`
	Description string `json:"description"`
}

// DivergentComponent represents a divergent component
type DivergentComponent struct {
	ResearchSpec string `json:"research_spec"`
	CodeImpl     string `json:"code_impl"`
	Impact       string `json:"impact"`
}

// Suggestion represents an improvement suggestion
type Suggestion struct {
	Type        string `json:"type"`
	Component   string `json:"component"`
	Description string `json:"description"`
}

// GeneratedCode represents generated code
type GeneratedCode struct {
	Language          string   `json:"language"`
	Code              string   `json:"code"`
	HeaderFile        string   `json:"header_file,omitempty"`
	BuildInstructions string   `json:"build_instructions"`
	Dependencies      []string `json:"dependencies"`
}

// AlignmentReport represents a full alignment analysis report
type AlignmentReport struct {
	Timestamp       time.Time                `json:"timestamp"`
	Research        ResearchExtract          `json:"research"`
	CodeAnalyses    []CodeAnalysis           `json:"code_analyses"`
	Alignments      []AlignmentResult        `json:"alignments"`
	CodeSuggestions map[string]GeneratedCode `json:"code_suggestions"`
	Summary         ReportSummary            `json:"summary"`
}

// ReportSummary represents summary of alignment report
type ReportSummary struct {
	TotalFilesAnalyzed int      `json:"total_files_analyzed"`
	AverageAlignment   float64  `json:"average_alignment"`
	SupportedLanguages []string `json:"supported_languages"`
}

// ResearchAlignmentAgent is the main agent structure
type ResearchAlignmentAgent struct {
	SupportedLanguages []string
	ResearchCache      map[string]ResearchExtract
	CodeAnalyses       map[string]CodeAnalysis
	AlignmentHistory   []AlignmentResult
}

// NewResearchAlignmentAgent creates a new ResearchAlignmentAgent
func NewResearchAlignmentAgent() *ResearchAlignmentAgent {
	return &ResearchAlignmentAgent{
		SupportedLanguages: []string{"C++", "Go", "R", "DLL"},
		ResearchCache:      make(map[string]ResearchExtract),
		CodeAnalyses:       make(map[string]CodeAnalysis),
		AlignmentHistory:   make([]AlignmentResult, 0),
	}
}

// DetectLanguage detects programming language from file extension
func (a *ResearchAlignmentAgent) DetectLanguage(extension string) string {
	extension = strings.ToLower(extension)

	langMap := map[string]string{
		".cpp": "C++", ".cc": "C++", ".cxx": "C++",
		".h": "C++", ".hpp": "C++", ".hxx": "C++",
		".go": "Go",
		".r":  "R", ".R": "R",
		".dll": "DLL",
	}

	if lang, ok := langMap[extension]; ok {
		return lang
	}
	return "unknown"
}

// ParseResearchDocument parses a research document
func (a *ResearchAlignmentAgent) ParseResearchDocument(documentPath string, documentContent string) (*ResearchExtract, error) {
	if documentPath != "" {
		content, err := os.ReadFile(documentPath)
		if err != nil {
			return nil, fmt.Errorf("failed to read document: %w", err)
		}
		documentContent = string(content)
	}

	if documentContent == "" {
		return nil, fmt.Errorf("no document content provided")
	}

	// Parse the document to extract real research information
	extract := &ResearchExtract{
		Title:       extractTitle(documentContent),
		Authors:     extractAuthors(documentContent),
		Algorithms:  extractAlgorithms(documentContent),
		Methods:     extractMethods(documentContent),
		KeyFormulas: extractFormulas(documentContent),
		Pseudocode:  extractPseudocode(documentContent),
		Timestamp:   time.Now(),
	}

	cacheKey := documentPath
	if cacheKey == "" {
		cacheKey = fmt.Sprintf("inline_%d", time.Now().UnixNano())
	}
	a.ResearchCache[cacheKey] = *extract

	return extract, nil
}

func extractTitle(content string) string {
	re := regexp.MustCompile(`(?m)^# (.*)$`)
	matches := re.FindAllStringSubmatch(content, -1)
	if len(matches) > 0 {
		return matches[0][1]
	}
	return ""
}

func extractAuthors(content string) []string {
	re := regexp.MustCompile(`(?m)^## Authors: (.*)$`)
	matches := re.FindAllStringSubmatch(content, -1)
	if len(matches) > 0 {
		return strings.Split(matches[0][1], ", ")
	}
	return []string{}
}

func extractAlgorithms(content string) []Algorithm {
	re := regexp.MustCompile(`(?m)^### Algorithm: (.*)$`)
	matches := re.FindAllStringSubmatch(content, -1)
	algorithms := make([]Algorithm, len(matches))
	for i, match := range matches {
		algorithms[i] = Algorithm{
			Name:        match[1],
			Description: "",
			Steps:       []string{},
			Complexity:  "",
			Parameters:  map[string]string{},
		}
	}
	return algorithms
}

func extractMethods(content string) []Method {
	re := regexp.MustCompile(`(?m)^### Method: (.*)$`)
	matches := re.FindAllStringSubmatch(content, -1)
	methods := make([]Method, len(matches))
	for i, match := range matches {
		methods[i] = Method{
			Name:        match[1],
			Description: "",
			Inputs:      []string{},
			Outputs:     []string{},
		}
	}
	return methods
}

func extractFormulas(content string) []string {
	re := regexp.MustCompile(`(?m)^### Formula: (.*)$`)
	matches := re.FindAllStringSubmatch(content, -1)
	formulas := make([]string, len(matches))
	for i, match := range matches {
		formulas[i] = match[1]
	}
	return formulas
}

func extractPseudocode(content string) []string {
	re := regexp.MustCompile(`(?m)^### Pseudocode: (.*)$`)
	matches := re.FindAllStringSubmatch(content, -1)
	pseudocode := make([]string, len(matches))
	for i, match := range matches {
		pseudocode[i] = match[1]
	}
	return pseudocode
}

// AnalyzeCodebase analyzes existing code
func (a *ResearchAlignmentAgent) AnalyzeCodebase(codePath string, codeContent string, language string) (*CodeAnalysis, error) {
	if codePath != "" {
		content, err := os.ReadFile(codePath)
		if err != nil {
			return nil, fmt.Errorf("failed to read code file: %w", err)
		}
		codeContent = string(content)
		language = a.DetectLanguage(filepath.Ext(codePath))
	}

	if codeContent == "" {
		return nil, fmt.Errorf("no code content provided")
	}

	analysis := &CodeAnalysis{
		FilePath:              codePath,
		Language:              language,
		Functions:             []Function{},
		Classes:               []Class{},
		AlgorithmsImplemented: []string{},
		Dependencies:          []string{},
		ComplexityMetrics:     make(map[string]interface{}),
	}

	cacheKey := codePath
	if cacheKey == "" {
		cacheKey = fmt.Sprintf("code_%d", time.Now().UnixNano())
	}
	a.CodeAnalyses[cacheKey] = *analysis

	return analysis, nil
}

// CheckAlignment checks alignment between research and code
func (a *ResearchAlignmentAgent) CheckAlignment(researchKey string, codeKey string) (*AlignmentResult, error) {
	researchExtract, hasResearch := a.ResearchCache[researchKey]
	if !hasResearch {
		return nil, fmt.Errorf("no research document found for key: %s", researchKey)
	}

	codeAnalysis, hasCode := a.CodeAnalyses[codeKey]
	if !hasCode {
		return nil, fmt.Errorf("no code analysis found for key: %s", codeKey)
	}

	// Perform actual alignment analysis
	alignedComponents := []AlignedComponent{}
	missingComponents := []MissingComponent{}
	divergentComponents := []DivergentComponent{}
	suggestions := []Suggestion{}

	// Check each algorithm from research against code
	for _, algorithm := range researchExtract.Algorithms {
		found := false
		for _, implAlgo := range codeAnalysis.AlgorithmsImplemented {
			if strings.EqualFold(algorithm.Name, implAlgo) ||
				strings.Contains(strings.ToLower(implAlgo), strings.ToLower(algorithm.Name)) {
				found = true
				alignedComponents = append(alignedComponents, AlignedComponent{
					ResearchComponent: algorithm.Name,
					CodeComponent:     implAlgo,
					Quality:           "high",
				})
				break
			}
		}

		if !found {
			// Check if partially implemented via function name matching
			partialMatch := ""
			for _, fn := range codeAnalysis.Functions {
				if strings.Contains(strings.ToLower(fn.Name), strings.ToLower(algorithm.Name)) {
					partialMatch = fn.Name
					break
				}
			}

			if partialMatch != "" {
				divergentComponents = append(divergentComponents, DivergentComponent{
					ResearchSpec: algorithm.Name,
					CodeImpl:     partialMatch,
					Impact:       "Partial implementation - may not cover all algorithm steps",
				})
			} else {
				importance := "medium"
				if strings.Contains(algorithm.Complexity, "O(n²)") || strings.Contains(algorithm.Complexity, "O(n^2)") {
					importance = "high"
				}
				missingComponents = append(missingComponents, MissingComponent{
					Component:   algorithm.Name,
					Importance:  importance,
					Description: fmt.Sprintf("Missing %s affects %s", algorithm.Name, algorithm.Complexity),
				})
			}
		}
	}

	// Calculate alignment score
	alignmentScore := 0.5
	totalResearchItems := len(researchExtract.Algorithms) + len(researchExtract.Methods)
	if totalResearchItems > 0 {
		alignedCount := len(alignedComponents)
		partialCount := len(divergentComponents)
		alignmentScore = float64(alignedCount) / float64(totalResearchItems)
		alignmentScore += float64(partialCount) * 0.3 / float64(totalResearchItems)
		if alignmentScore > 1.0 {
			alignmentScore = 1.0
		}
	}

	// Determine overall status
	overallStatus := PartiallyAligned
	if alignmentScore >= 0.8 {
		overallStatus = Aligned
	} else if alignmentScore < 0.3 {
		overallStatus = NotAligned
	}

	result := &AlignmentResult{
		OverallStatus:       overallStatus,
		AlignmentScore:      alignmentScore,
		AlignedComponents:   alignedComponents,
		MissingComponents:   missingComponents,
		DivergentComponents: divergentComponents,
		Suggestions:         suggestions,
	}

	a.AlignmentHistory = append(a.AlignmentHistory, *result)
	return result, nil
}

// GenerateAlignedCode generates code aligned with research
func (a *ResearchAlignmentAgent) GenerateAlignedCode(researchKey string, targetLanguage string, component string) (*GeneratedCode, error) {
	supported := false
	for _, lang := range a.SupportedLanguages {
		if lang == targetLanguage {
			supported = true
			break
		}
	}
	if !supported {
		return nil, fmt.Errorf("language %s not supported. Supported: %s", targetLanguage, strings.Join(a.SupportedLanguages, ", "))
	}

	researchExtract, hasResearch := a.ResearchCache[researchKey]
	if !hasResearch {
		return nil, fmt.Errorf("no research document found for key: %s", researchKey)
	}

	var code, headerFile, buildInstructions string
	var dependencies []string

	switch targetLanguage {
	case "C++":
		code = a.generateCppTemplate(researchExtract, component)
		headerFile = a.generateCppHeaderTemplate(researchExtract, component)
		buildInstructions = "g++ -std=c++17 -o output main.cpp -Wall -Wextra"
		dependencies = []string{"C++17 or later", "CMake 3.14+"}
	case "Go":
		code = a.generateGoTemplate(researchExtract, component)
		buildInstructions = "go build -o output -v"
		dependencies = []string{"Go 1.21 or later", "Go modules"}
	case "R":
		code = a.generateRTemplate(researchExtract, component)
		buildInstructions = "Rscript main.R"
		dependencies = []string{"R 4.0 or later", "required packages: listed in file"}
	case "DLL":
		code = a.generateDLLTemplate(researchExtract, component)
		headerFile = a.generateDLLHeaderTemplate(researchExtract, component)
		buildInstructions = "cl /LD /Fe:output.dll main.cpp /O2"
		dependencies = []string{"Windows SDK", "MSVC C++ compiler", "C++17 or later"}
	default:
		return nil, fmt.Errorf("unsupported language: %s", targetLanguage)
	}

	return &GeneratedCode{
		Language:          targetLanguage,
		Code:              code,
		HeaderFile:        headerFile,
		BuildInstructions: buildInstructions,
		Dependencies:      dependencies,
	}, nil
}

// RunFullAlignmentAnalysis performs a complete alignment analysis
func (a *ResearchAlignmentAgent) RunFullAlignmentAnalysis(researchPath string, codePaths []string) (*AlignmentReport, error) {
	extract, err := a.ParseResearchDocument(researchPath, "")
	if err != nil {
		return nil, fmt.Errorf("failed to parse research: %w", err)
	}

	codeAnalyses := make([]CodeAnalysis, 0)
	for _, codePath := range codePaths {
		analysis, err := a.AnalyzeCodebase(codePath, "", "")
		if err != nil {
			fmt.Fprintf(os.Stderr, "Warning: Failed to analyze %s: %v\n", codePath, err)
			continue
		}
		codeAnalyses = append(codeAnalyses, *analysis)
	}

	alignments := make([]AlignmentResult, 0)
	for i := range codeAnalyses {
		researchKey := researchPath
		if researchKey == "" {
			for k := range a.ResearchCache {
				researchKey = k
				break
			}
		}
		codeKey := codeAnalyses[i].FilePath
		if codeKey == "" {
			codeKey = fmt.Sprintf("code_%d", i)
		}
		alignment, err := a.CheckAlignment(researchKey, codeKey)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Warning: Alignment check failed: %v\n", err)
			continue
		}
		alignments = append(alignments, *alignment)
	}

	codeSuggestions := make(map[string]GeneratedCode)
	for _, lang := range []string{"C++", "Go", "R"} {
		researchKey := researchPath
		if researchKey == "" {
			for k := range a.ResearchCache {
				researchKey = k
				break
			}
		}
		suggestion, err := a.GenerateAlignedCode(researchKey, lang, "")
		if err != nil {
			fmt.Fprintf(os.Stderr, "Warning: Failed to generate %s code: %v\n", lang, err)
			continue
		}
		codeSuggestions[lang] = *suggestion
	}

	var totalScore float64
	for _, alignment := range alignments {
		totalScore += alignment.AlignmentScore
	}
	avgScore := 0.0
	if len(alignments) > 0 {
		avgScore = totalScore / float64(len(alignments))
	}

	return &AlignmentReport{
		Timestamp:       time.Now(),
		Research:        *extract,
		CodeAnalyses:    codeAnalyses,
		Alignments:      alignments,
		CodeSuggestions: codeSuggestions,
		Summary: ReportSummary{
			TotalFilesAnalyzed: len(codeAnalyses),
			AverageAlignment:   avgScore,
			SupportedLanguages: a.SupportedLanguages,
		},
	}, nil
}

// GetAlignmentHistory returns recent alignment history
func (a *ResearchAlignmentAgent) GetAlignmentHistory(limit int) []map[string]interface{} {
	start := 0
	if len(a.AlignmentHistory) > limit {
		start = len(a.AlignmentHistory) - limit
	}

	history := make([]map[string]interface{}, 0)
	for _, alignment := range a.AlignmentHistory[start:] {
		entry := map[string]interface{}{
			"status":        alignment.OverallStatus,
			"score":         alignment.AlignmentScore,
			"missing_count": len(alignment.MissingComponents),
		}
		history = append(history, entry)
	}
	return history
}

// GetResearchCacheSummary returns summary of cached research documents
func (a *ResearchAlignmentAgent) GetResearchCacheSummary() map[string]map[string]interface{} {
	summary := make(map[string]map[string]interface{})
	for key, extract := range a.ResearchCache {
		summary[key] = map[string]interface{}{
			"title":            extract.Title,
			"algorithms_count": len(extract.Algorithms),
			"methods_count":    len(extract.Methods),
		}
	}
	return summary
}

func main() {
	agent := NewResearchAlignmentAgent()

	if len(os.Args) > 1 {
		command := os.Args[1]

		switch command {
		case "parse-research":
			if len(os.Args) < 3 {
				fmt.Println("Usage: research-alignment-agent parse-research <document-path>")
				os.Exit(1)
			}
			extract, err := agent.ParseResearchDocument(os.Args[2], "")
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(extract)

		case "analyze-code":
			if len(os.Args) < 3 {
				fmt.Println("Usage: research-alignment-agent analyze-code <code-path>")
				os.Exit(1)
			}
			analysis, err := agent.AnalyzeCodebase(os.Args[2], "", "")
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(analysis)

		case "generate-code":
			if len(os.Args) < 4 {
				fmt.Println("Usage: research-alignment-agent generate-code <research-key> <language>")
				os.Exit(1)
			}
			code, err := agent.GenerateAlignedCode(os.Args[2], os.Args[3], "")
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(code)

		case "full-analysis":
			codePaths := []string{}
			if len(os.Args) > 2 {
				codePaths = os.Args[2:]
			}
			report, err := agent.RunFullAlignmentAnalysis("", codePaths)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			json.NewEncoder(os.Stdout).Encode(report)

		default:
			fmt.Println("Research Alignment Agent")
			fmt.Println("Commands:")
			fmt.Println("  parse-research <path>       - Parse a research document")
			fmt.Println("  analyze-code <path>         - Analyze code file")
			fmt.Println("  generate-code <key> <lang>  - Generate aligned code (C++, Go, R, DLL)")
			fmt.Println("  full-analysis [code...]     - Run full alignment analysis")
		}
	} else {
		fmt.Println("Research Alignment Agent - Validates code against research")
		fmt.Println("Supported languages: C++, DLL, Go, R")
		fmt.Println("\nUsage: research-alignment-agent <command> [args]")
	}
}

// Template generation methods

func (a *ResearchAlignmentAgent) generateCppTemplate(extract ResearchExtract, component string) string {
	code := fmt.Sprintf(`// C++ Implementation: %s
// Based on research: %s
// Auto-generated by Research Alignment Agent

#include <iostream>
#include <vector>
#include <string>
#include <memory>

namespace research_impl {

`, component, extract.Title)

	// Generate class stubs for each algorithm
	for _, algo := range extract.Algorithms {
		code += fmt.Sprintf(`// %s - %s
// Complexity: %s
class %s {
public:
    %s() = default;
    ~%s() = default;
    
    // Initialize with parameters from research
    void initialize(%s) {
        // TODO: Initialize with specific parameters
    }
    
    // Main execution method
    auto execute() {
        // Steps from research:
`, algo.Name, algo.Description, algo.Complexity, algo.Name, algo.Name, algo.Name, algo.Parameters)

		for i, step := range algo.Steps {
			code += fmt.Sprintf(`        // Step %d: %s
`, i+1, step)
		}

		code += fmt.Sprintf(`        return nullptr; // TODO: Implement return value
    }
};

`)
	}

	code += `} // namespace research_impl

int main() {
    // Example usage
    research_impl::Algorithm instance;
    instance.initialize();
    auto result = instance.execute();
    return 0;
}
`
	return code
}

func (a *ResearchAlignmentAgent) generateCppHeaderTemplate(extract ResearchExtract, component string) string {
	header := fmt.Sprintf(`#pragma once
// Header for: %s
// Based on research: %s

#include <vector>
#include <string>
#include <memory>

namespace research_impl {
`, component, extract.Title)

	// Forward declarations
	for _, algo := range extract.Algorithms {
		header += fmt.Sprintf(`
// %s - %s
class %s {
public:
    %s();
    ~%s();
    void initialize();
    auto execute();
};
`, algo.Name, algo.Description, algo.Name, algo.Name, algo.Name)
	}

	header += `
} // namespace research_impl
`
	return header
}

func (a *ResearchAlignmentAgent) generateGoTemplate(extract ResearchExtract, component string) string {
	code := fmt.Sprintf(`// Go Implementation: %s
// Based on research: %s
// Auto-generated by Research Alignment Agent

package main

import (
	"fmt"
	"log"
)

`, component, extract.Title)

	// Generate struct and methods for each algorithm
	for _, algo := range extract.Algorithms {
		code += fmt.Sprintf(`// %s implements the %s algorithm
// Complexity: %s
type %s struct {
	// Add fields based on research parameters
}

// New%s creates a new instance
func New%s() *%s {
	return &%s{}
}

// Execute runs the algorithm
func (a *%s) Execute() (interface{}, error) {
	// Algorithm steps from research:
`, algo.Name, algo.Description, algo.Complexity, algo.Name, algo.Name, algo.Name, algo.Name, algo.Name, algo.Name)

		for i, step := range algo.Steps {
			code += fmt.Sprintf(`	// Step %d: %s
`, i+1, step)
		}

		code += fmt.Sprintf(`	return nil, fmt.Errorf("not implemented: %s")
}

`, algo.Name)
	}

	code += `func main() {
	// Example usage
	instance := NewAlgorithm()
	result, err := instance.Execute()
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(result)
}
`
	return code
}

func (a *ResearchAlignmentAgent) generateRTemplate(extract ResearchExtract, component string) string {
	code := fmt.Sprintf(`# R Implementation: %s
# Based on research: %s
# Auto-generated by Research Alignment Agent

`, component, extract.Title)

	// Generate functions for each algorithm
	for _, algo := range extract.Algorithms {
		code += fmt.Sprintf(`
#' %s
#' %s
#' @export
%s <- function(params = list()) {
  # Algorithm steps from research:
`, algo.Name, algo.Description, algo.Name)

		for i, step := range algo.Steps {
			code += fmt.Sprintf(`  # Step %d: %s
`, i+1, step)
		}

		code += fmt.Sprintf(`  
  stop("Not implemented: %s")
}
`, algo.Name)
	}

	code += `
# Example usage
# result <- algorithm(params=list())
`
	return code
}

func (a *ResearchAlignmentAgent) generateDLLTemplate(extract ResearchExtract, component string) string {
	code := fmt.Sprintf(`// DLL Implementation: %s
// Based on research: %s
// Auto-generated by Research Alignment Agent

#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BUILDING_DLL
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT __declspec(dllimport)
#endif

`, component, extract.Title)

	// Generate export functions for each algorithm
	for _, algo := range extract.Algorithms {
		code += fmt.Sprintf(`
// %s - %s
// Complexity: %s
DLL_EXPORT int32_t %s_Initialize(void);
DLL_EXPORT int32_t %s_Execute(void* input, void* output);
DLL_EXPORT void %s_Cleanup(void);
`, algo.Name, algo.Description, algo.Complexity, algo.Name, algo.Name, algo.Name)
	}

	code += `

#ifdef __cplusplus
}
#endif
`
	return code
}

func (a *ResearchAlignmentAgent) generateDLLHeaderTemplate(extract ResearchExtract, component string) string {
	header := fmt.Sprintf(`#pragma once
// DLL Header: %s
// Based on research: %s

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BUILDING_DLL
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT __declspec(dllimport)
#endif

`, component, extract.Title)

	// Export declarations
	for _, algo := range extract.Algorithms {
		header += fmt.Sprintf(`
// %s API
DLL_EXPORT int32_t %s_Initialize(void);
DLL_EXPORT int32_t %s_Execute(void* input, void* output);
DLL_EXPORT void %s_Cleanup(void);
`, algo.Name, algo.Name, algo.Name, algo.Name)
	}

	header += `
#ifdef __cplusplus
}
#endif
`
	return header
}
