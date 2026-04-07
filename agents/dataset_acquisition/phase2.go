package main

import (
	"fmt"
	"log"
)

// RunPhase2STEM handles STEM and Social Sciences datasets
func RunPhase2STEM() error {
	var entries []DatasetEntry

	stemTopics := []string{
		"Psychology",
		"Sociology",
		"Anthropology",
		"Economics",
		"Political science",
		"Neuroscience",
	}
	for _, topic := range stemTopics {
		summary, sourceURL, err := FetchWikipediaSummary(topic)
		if err != nil {
			log.Printf("stem topic fetch failed (%s): %v", topic, err)
			continue
		}
		entries = append(entries, DatasetEntry{
			Text:        fmt.Sprintf("%s: %s", topic, summary),
			Source:      "Wikipedia",
			Domain:      "SocialSciences",
			URL:         sourceURL,
			RetrievedAt: NowRFC3339(),
		})
	}

	philTopics := []string{
		"Logic",
		"Ethics",
		"Metaphysics",
		"Epistemology",
		"Philosophy of science",
	}
	for _, topic := range philTopics {
		summary, sourceURL, err := FetchWikipediaSummary(topic)
		if err != nil {
			log.Printf("philosophy topic fetch failed (%s): %v", topic, err)
			continue
		}
		entries = append(entries, DatasetEntry{
			Text:        fmt.Sprintf("%s: %s", topic, summary),
			Source:      "Wikipedia",
			Domain:      "Philosophy",
			URL:         sourceURL,
			RetrievedAt: NowRFC3339(),
		})
	}

	if len(entries) == 0 {
		return fmt.Errorf("phase2 STEM produced no entries")
	}

	return WriteJSONL(getPhaseFile("phase2_stem"), entries)
}

// RunPhase2Quantum handles practical Quantum Computing datasets (Clifford gates)
func RunPhase2Quantum() error {
	var entries []DatasetEntry

	quantumTopics := []string{
		"Clifford gate",
		"Hadamard gate",
		"Pauli matrices",
		"Controlled NOT gate",
		"Gottesman–Knill theorem",
		"Quantum error correction",
	}
	for _, topic := range quantumTopics {
		summary, sourceURL, err := FetchWikipediaSummary(topic)
		if err != nil {
			log.Printf("quantum topic fetch failed (%s): %v", topic, err)
			continue
		}
		entries = append(entries, DatasetEntry{
			Text:        fmt.Sprintf("%s: %s", topic, summary),
			Source:      "Wikipedia",
			Domain:      "Quantum/CliffordGates",
			URL:         sourceURL,
			RetrievedAt: NowRFC3339(),
		})
	}

	if len(entries) == 0 {
		return fmt.Errorf("phase2 quantum produced no entries")
	}

	return WriteJSONL(getPhaseFile("phase2_quantum"), entries)
}
