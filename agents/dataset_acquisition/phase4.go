package main

import (
	"encoding/xml"
	"fmt"
	"log"
)

// RunPhase4Academia handles downloading paper abstracts from ArXiv API
func RunPhase4Academia() error {
	var entries []DatasetEntry

	// Use ArXiv API to fetch abstracts for quant-ph (Quantum Physics) and math (Mathematics)
	// Query: quantum computing OR mathematics
	arxivURL := "http://export.arxiv.org/api/query?search_query=cat:quant-ph+OR+cat:math&start=0&max_results=50"
	
	log.Printf("Fetching ArXiv abstracts from %s...", arxivURL)
	body, err := FetchURL(arxivURL)
	if err != nil {
		return fmt.Errorf("failed to fetch from ArXiv: %v", err)
	}

	type Entry struct {
		Title   string `xml:"title"`
		Summary string `xml:"summary"`
	}

	type Feed struct {
		Entries []Entry `xml:"entry"`
	}

	var feed Feed
	if err := xml.Unmarshal(body, &feed); err == nil {
		for _, e := range feed.Entries {
			entries = append(entries, DatasetEntry{
				Text:   fmt.Sprintf("Title: %s\nAbstract: %s", e.Title, e.Summary),
				Source: "ArXiv",
				Domain: "Academia/Advanced",
			})
		}
	} else {
		log.Printf("Warning: failed to parse ArXiv XML: %v", err)
		
		// Fallback static data if API parsing fails
		entries = append(entries, DatasetEntry{
			Text: "Theoretical quantum mechanics is a fundamental theory in physics that provides a description of the physical properties of nature at the scale of atoms and subatomic particles.",
			Source: "ArXivFallback",
			Domain: "Academia/QuantumTheory",
		})
	}

	return WriteJSONL(getPhaseFile("phase4_acad"), entries)
}
