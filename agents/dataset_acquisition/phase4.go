package main

import (
	"encoding/xml"
	"fmt"
	"log"
	"strings"
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
			text := NormalizeText(fmt.Sprintf("Title: %s\nAbstract: %s", e.Title, e.Summary))
			if text == "" {
				continue
			}
			entries = append(entries, DatasetEntry{
				Text:        text,
				Source:      "ArXiv",
				Domain:      "Academia/Advanced",
				URL:         arxivURL,
				RetrievedAt: NowRFC3339(),
			})
		}
		if len(entries) == 0 {
			return fmt.Errorf("parsed arXiv feed but produced zero valid entries")
		}
	} else {
		return fmt.Errorf("failed to parse ArXiv XML response: %w", err)
	}

	if len(entries) < 10 {
		return fmt.Errorf("insufficient arXiv records retrieved: %d", len(entries))
	}

	for i := range entries {
		entries[i].Text = strings.TrimSpace(entries[i].Text)
	}

	return WriteJSONL(getPhaseFile("phase4_acad"), entries)
}
