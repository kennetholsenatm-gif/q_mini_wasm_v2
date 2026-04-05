package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

// ResearchDoc represents scanned research document
type ResearchDoc struct {
	Path    string `json:"path"`
	Name    string `json:"name"`
	Content string `json:"content"`
	Size    int    `json:"size"`
}

// ConceptCoverage represents coverage status for a core concept
type ConceptCoverage struct {
	Name       string   `json:"name"`
	Found      bool     `json:"found"`
	Mentions   int      `json:"mentions"`
	FoundIn    []string `json:"found_in"`
}

// CoverageReport contains full research coverage analysis
type CoverageReport struct {
	CoreConceptsCount  int               `json:"core_concepts_count"`
	ResearchFilesCount int               `json:"research_files_count"`
	TotalMentions      int               `json:"total_mentions"`
	CoveragePercent    float64           `json:"coverage_percent"`
	Coverage           []ConceptCoverage `json:"coverage"`
	ConceptGaps        []string          `json:"concept_gaps"`
	ResearchFiles      []ResearchDoc     `json:"research_files"`
}

// ResearchCoverageAnalyzer scans research docs against core concepts
type ResearchCoverageAnalyzer struct {
	ResearchDir string
	ConfigPath  string
}

// NewResearchCoverageAnalyzer creates new analyzer instance
func NewResearchCoverageAnalyzer(researchDir string, configPath string) *ResearchCoverageAnalyzer {
	return &ResearchCoverageAnalyzer{
		ResearchDir: researchDir,
		ConfigPath:  configPath,
	}
}

// LoadCoreConcepts loads core concepts from JSON definition
func (rca *ResearchCoverageAnalyzer) LoadCoreConcepts() (map[string]interface{}, error) {
	data, err := os.ReadFile(rca.ConfigPath)
	if err != nil {
		return nil, err
	}

	var concepts map[string]interface{}
	err = json.Unmarshal(data, &concepts)
	return concepts, err
}

// ScanResearchDocs scans research directory for markdown files
func (rca *ResearchCoverageAnalyzer) ScanResearchDocs() ([]ResearchDoc, error) {
	var docs []ResearchDoc

	err := filepath.WalkDir(rca.ResearchDir, func(path string, d os.DirEntry, err error) error {
		if err != nil {
			return nil
		}

		if d.IsDir() {
			return nil
		}

		if strings.ToLower(filepath.Ext(path)) != ".md" {
			return nil
		}

		content, err := os.ReadFile(path)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Warning: Could not read %s: %v\n", path, err)
			return nil
		}

		relPath, err := filepath.Rel(filepath.Dir(rca.ResearchDir), path)
		if err != nil {
			relPath = path
		}

		docs = append(docs, ResearchDoc{
			Path:    relPath,
			Name:    d.Name(),
			Content: string(content),
			Size:    len(content),
		})

		return nil
	})

	return docs, err
}

// AnalyzeCoverage performs full coverage analysis
func (rca *ResearchCoverageAnalyzer) AnalyzeCoverage() (*CoverageReport, error) {
	coreConcepts, err := rca.LoadCoreConcepts()
	if err != nil {
		return nil, err
	}

	researchDocs, err := rca.ScanResearchDocs()
	if err != nil {
		return nil, err
	}

	conceptList := make([]string, 0)
	if concepts, ok := coreConcepts["concepts"].([]interface{}); ok {
		for _, c := range concepts {
			if s, ok := c.(string); ok {
				conceptList = append(conceptList, s)
			}
		}
	}

	coverageMap := make(map[string]*ConceptCoverage)
	for _, concept := range conceptList {
		coverageMap[concept] = &ConceptCoverage{
			Name:     concept,
			Found:    false,
			Mentions: 0,
			FoundIn:  []string{},
		}
	}

	totalMentions := 0

	for _, doc := range researchDocs {
		for concept, coverage := range coverageMap {
			re := regexp.MustCompile(`(?i)` + regexp.QuoteMeta(concept))
			matches := re.FindAllStringIndex(doc.Content, -1)
			if len(matches) > 0 {
				coverage.Found = true
				coverage.Mentions += len(matches)
				coverage.FoundIn = append(coverage.FoundIn, doc.Path)
				totalMentions += len(matches)
			}
		}
	}

	var coverage []ConceptCoverage
	var gaps []string
	for _, c := range coverageMap {
		coverage = append(coverage, *c)
		if !c.Found {
			gaps = append(gaps, c.Name)
		}
	}

	found := 0
	for _, c := range coverage {
		if c.Found {
			found++
		}
	}

	percent := 0.0
	if len(conceptList) > 0 {
		percent = float64(found) / float64(len(conceptList)) * 100
	}

	report := &CoverageReport{
		CoreConceptsCount:  len(conceptList),
		ResearchFilesCount: len(researchDocs),
		TotalMentions:      totalMentions,
		CoveragePercent:    percent,
		Coverage:           coverage,
		ConceptGaps:        gaps,
		ResearchFiles:      researchDocs,
	}

	return report, nil
}

func main() {
	researchDir := flag.String("research-dir", "docs/research", "Directory containing research documentation")
	configPath := flag.String("config", "config/core-concepts.json", "Path to core concepts JSON file")
	outputFile := flag.String("output", "", "Output file for JSON report")

	flag.Parse()

	analyzer := NewResearchCoverageAnalyzer(*researchDir, *configPath)
	report, err := analyzer.AnalyzeCoverage()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("\n✅ Research Coverage Analysis Complete\n")
	fmt.Printf("   Core Concepts: %d\n", report.CoreConceptsCount)
	fmt.Printf("   Research Files: %d\n", report.ResearchFilesCount)
	fmt.Printf("   Coverage: %.1f%%\n", report.CoveragePercent)
	fmt.Printf("   Total Mentions: %d\n\n", report.TotalMentions)

	if len(report.ConceptGaps) > 0 {
		fmt.Printf("⚠️  Missing Concepts (%d):\n", len(report.ConceptGaps))
		for _, gap := range report.ConceptGaps {
			fmt.Printf("   - %s\n", gap)
		}
	} else {
		fmt.Printf("✅ All core concepts covered\n")
	}

	if *outputFile != "" {
		data, _ := json.MarshalIndent(report, "", "  ")
		os.WriteFile(*outputFile, data, 0644)
		fmt.Printf("\n📄 Report saved to: %s\n", *outputFile)
	}
}