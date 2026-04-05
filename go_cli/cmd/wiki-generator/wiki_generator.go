package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

// WikiGenerator converts docs/ directory structure into GitHub Wiki pages
// Follows Cognitive Ergonomics principles: Miller's Law, Progressive Disclosure
type WikiGenerator struct {
	RepoRoot   string
	DocsDir    string
	OutputDir  string
}

// NewWikiGenerator creates a new WikiGenerator
func NewWikiGenerator() *WikiGenerator {
	execPath, _ := os.Executable()
	repoRoot := filepath.Join(filepath.Dir(execPath), "..", "..")
	absRoot, _ := filepath.Abs(repoRoot)

	return &WikiGenerator{
		RepoRoot:  absRoot,
		DocsDir:   filepath.Join(absRoot, "docs"),
		OutputDir: filepath.Join(absRoot, "wiki-output"),
	}
}

// GetSidebarCategories returns sidebar categories (Miller's Law: max 7 items)
func (g *WikiGenerator) GetSidebarCategories() []interface{} {
	return []interface{}{
		[]string{"Home", "Home.md"},
		[]interface{}{"Architecture", []interface{}{
			[]string{"Overview", "Architecture-Overview.md"},
			[]string{"Ternary State Space", "Architecture-Ternary-State-Space.md"},
			[]string{"Stabilizer Tableau", "Architecture-Stabilizer-Tableau.md"},
			[]string{"MoE Routing", "Architecture-MoE-Routing.md"},
			[]string{"Forward-Forward", "Architecture-Forward-Forward.md"},
			[]string{"SYCL Acceleration", "Architecture-SYCL-Acceleration.md"},
		}},
		[]interface{}{"API Reference", []interface{}{
			[]string{"Core API", "API-Core-Reference.md"},
		}},
		[]interface{}{"Guides", []interface{}{
			[]string{"Building", "Guides-Building.md"},
			[]string{"SYCL Setup", "Guides-SYCL-Setup.md"},
			[]string{"Contributing", "Guides-Contributing.md"},
		}},
		[]interface{}{"Research", []interface{}{
			[]string{"Cognitive Ergonomics", "Research-Cognitive-Ergonomics.md"},
			[]string{"Clifford Entanglement", "Research-Clifford-Entanglement.md"},
			[]string{"Framework Synthesis", "Research-Framework-Synthesis.md"},
		}},
		[]interface{}{"Decisions", []interface{}{
			[]string{"ADR-001: Ternary", "Decisions-ADR-001-Ternary.md"},
		}},
	}
}

// GenerateSidebar generates _Sidebar.md for GitHub Wiki
func (g *WikiGenerator) GenerateSidebar(categories []interface{}) string {
	var buf bytes.Buffer

	for _, item := range categories {
		switch v := item.(type) {
		case []string:
			buf.WriteString(fmt.Sprintf("* [%s](%s)\n", v[0], v[1]))
		case []interface{}:
			name := v[0].(string)
			buf.WriteString(fmt.Sprintf("**%s**\n", name))
			for _, sub := range v[1].([]interface{}) {
				subItem := sub.([]string)
				buf.WriteString(fmt.Sprintf("  * [%s](%s)\n", subItem[0], subItem[1]))
			}
		}
	}

	return buf.String()
}

// GenerateHome generates Home.md from root README
func (g *WikiGenerator) GenerateHome() string {
	readmePath := filepath.Join(g.RepoRoot, "README.md")
	data, err := os.ReadFile(readmePath)
	if err != nil {
		return "# q_mini_wasm_v2\n\nWelcome to the wiki."
	}

	content := string(data)

	// Replace relative links to wiki-style
	re := regexp.MustCompile(`\[([^\]]+)\]\(docs/([^)]+)`)
	content = re.ReplaceAllString(content, `[$1]($2`)

	return content
}

// ConvertDocToWiki converts a docs/ markdown file to wiki format
func (g *WikiGenerator) ConvertDocToWiki(sourcePath string) (string, error) {
	data, err := os.ReadFile(sourcePath)
	if err != nil {
		return "", err
	}

	content := string(data)

	// Fix relative links for wiki
	re1 := regexp.MustCompile(`\[([^\]]+)\]\(\.\./([^)]+)`)
	content = re1.ReplaceAllString(content, `[$1]($2`)

	re2 := regexp.MustCompile(`\[([^\]]+)\]\(([^)]+)\.md\)`)
	content = re2.ReplaceAllString(content, `[$1]($2`)

	return content, nil
}

// Generate executes full wiki generation
func (g *WikiGenerator) Generate() (int, error) {
	// Clean output directory
	if _, err := os.Stat(g.OutputDir); !os.IsNotExist(err) {
		if err := os.RemoveAll(g.OutputDir); err != nil {
			return 0, err
		}
	}

	if err := os.MkdirAll(g.OutputDir, 0755); err != nil {
		return 0, err
	}

	wikiFiles := make(map[string]string)

	// Home page
	wikiFiles["Home.md"] = g.GenerateHome()

	// Architecture mappings
	archMapping := map[string]string{
		"architecture/overview.md":              "Architecture-Overview.md",
		"architecture/ternary-state-space.md":   "Architecture-Ternary-State-Space.md",
		"architecture/stabilizer-tableau.md":    "Architecture-Stabilizer-Tableau.md",
		"architecture/moe-routing.md":           "Architecture-MoE-Routing.md",
		"architecture/forward-forward.md":       "Architecture-Forward-Forward.md",
		"architecture/sycl-acceleration.md":     "Architecture-SYCL-Acceleration.md",
	}

	for src, dst := range archMapping {
		srcPath := filepath.Join(g.DocsDir, src)
		if content, err := g.ConvertDocToWiki(srcPath); err == nil {
			wikiFiles[dst] = content
		}
	}

	// API docs
	if apiContent, err := g.ConvertDocToWiki(filepath.Join(g.DocsDir, "api", "core-reference.md")); err == nil {
		wikiFiles["API-Core-Reference.md"] = apiContent
	}

	// Guides mappings
	guideMapping := map[string]string{
		"guides/building.md":       "Guides-Building.md",
		"guides/sycl-setup.md":     "Guides-SYCL-Setup.md",
		"guides/contributing.md":   "Guides-Contributing.md",
	}

	for src, dst := range guideMapping {
		srcPath := filepath.Join(g.DocsDir, src)
		if content, err := g.ConvertDocToWiki(srcPath); err == nil {
			wikiFiles[dst] = content
		}
	}

	// Decisions
	if adrContent, err := g.ConvertDocToWiki(filepath.Join(g.DocsDir, "decisions", "adr-001-ternary-over-binary.md")); err == nil {
		wikiFiles["Decisions-ADR-001-Ternary.md"] = adrContent
	}

	// Research papers
	researchMapping := map[string]string{
		"research/Cognitive Ergonomics Model Protocol Research.md":      "Research-Cognitive-Ergonomics.md",
		"research/Enhancing Framework with Clifford Entanglement.md":    "Research-Clifford-Entanglement.md",
		"research/QMINIWASM_ Quantum-Classical Framework Synthesis.md":  "Research-Framework-Synthesis.md",
	}

	for src, dst := range researchMapping {
		srcPath := filepath.Join(g.DocsDir, src)
		if data, err := os.ReadFile(srcPath); err == nil {
			wikiFiles[dst] = string(data)
		}
	}

	// Sidebar
	categories := g.GetSidebarCategories()
	wikiFiles["_Sidebar.md"] = g.GenerateSidebar(categories)

	// Write all files
	for name, content := range wikiFiles {
		outputPath := filepath.Join(g.OutputDir, name)
		if err := os.WriteFile(outputPath, []byte(content), 0644); err != nil {
			return len(wikiFiles), err
		}
		fmt.Printf("  Created: %s\n", name)
	}

	fmt.Printf("\nWiki generated: %d pages -> %s\n", len(wikiFiles), g.OutputDir)
	return len(wikiFiles), nil
}

func main() {
	fmt.Println("Generating wiki from docs/...")

	generator := NewWikiGenerator()
	count, err := generator.Generate()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}