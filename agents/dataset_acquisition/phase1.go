package main

import (
	"encoding/json"
	"fmt"
	"log"
	"sort"
	"strings"
)

// RunPhase1NLPMath handles NLP and Math datasets
func RunPhase1NLPMath() error {
	var entries []DatasetEntry

	// Download an open English Dictionary (JSON format)
	// Example: A well-known open source english dictionary JSON
	dictURL := "https://raw.githubusercontent.com/matthewreagan/WebstersEnglishDictionary/master/dictionary.json"
	log.Printf("Fetching dictionary from %s...", dictURL)

	body, err := FetchURL(dictURL)
	if err != nil {
		return fmt.Errorf("failed to fetch dictionary: %v", err)
	}

	var dict map[string]string
	if err := json.Unmarshal(body, &dict); err == nil {
		keys := make([]string, 0, len(dict))
		for k := range dict {
			keys = append(keys, k)
		}
		sort.Strings(keys)

		// Process all dictionary entries - no artificial limit
		for i := 0; i < len(keys); i++ {
			word := keys[i]
			definition := dict[word]
			if len(definition) == 0 {
				continue
			}
			entries = append(entries, DatasetEntry{
				Text:        fmt.Sprintf("%s: %s", strings.Title(word), definition),
				Source:      "WebstersEnglishDictionary",
				Domain:      "NLP/Dictionary",
				URL:         dictURL,
				RetrievedAt: NowRFC3339(),
			})
		}
	} else {
		return fmt.Errorf("failed to parse dictionary response: %w", err)
	}

	mathTopics := []string{
		"Algebra",
		"Calculus",
		"Geometry",
		"Number theory",
		"Linear algebra",
		"Discrete mathematics",
		"Graph theory",
		"Probability theory",
		"Statistics",
		"Topology",
	}
	for _, topic := range mathTopics {
		summary, sourceURL, err := FetchWikipediaSummary(topic)
		if err != nil {
			log.Printf("math topic fetch failed (%s): %v", topic, err)
			continue
		}
		entries = append(entries, DatasetEntry{
			Text:        fmt.Sprintf("%s: %s", topic, summary),
			Source:      "Wikipedia",
			Domain:      "Mathematics",
			URL:         sourceURL,
			RetrievedAt: NowRFC3339(),
		})
	}

	if len(entries) == 0 {
		return fmt.Errorf("phase1 NLP/Math produced no entries")
	}

	return WriteJSONL(getPhaseFile("phase1_nlp"), entries)
}

// RunPhase1Programming handles C++, R, and Go documentation / snippets
func RunPhase1Programming() error {
	var entries []DatasetEntry

	goTopics := []string{
		"Go programming language",
		"Goroutine",
		"Go channels",
		"Go modules",
	}
	cppTopics := []string{
		"C++",
		"RAII",
		"Template metaprogramming",
		"C++ memory management",
	}
	rTopics := []string{
		"R (programming language)",
		"Data frame",
		"ggplot2",
		"Statistical computing",
	}

	fetchTopic := func(topic, domain string) {
		summary, sourceURL, err := FetchWikipediaSummary(topic)
		if err != nil {
			log.Printf("programming topic fetch failed (%s): %v", topic, err)
			return
		}
		entries = append(entries, DatasetEntry{
			Text:        fmt.Sprintf("%s: %s", topic, summary),
			Source:      "Wikipedia",
			Domain:      domain,
			URL:         sourceURL,
			RetrievedAt: NowRFC3339(),
		})
	}

	for _, topic := range goTopics {
		fetchTopic(topic, "Programming/Go")
	}
	for _, topic := range cppTopics {
		fetchTopic(topic, "Programming/C++")
	}
	for _, topic := range rTopics {
		fetchTopic(topic, "Programming/R")
	}

	if len(entries) == 0 {
		return fmt.Errorf("phase1 programming produced no entries")
	}

	return WriteJSONL(getPhaseFile("phase1_prog"), entries)
}
