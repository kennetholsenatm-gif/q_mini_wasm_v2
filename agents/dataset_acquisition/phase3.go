package main

import (
	"fmt"
	"log"
)

// RunPhase3CorporateWiki handles Corporate Documentation and Wikipedia Extracts
func RunPhase3CorporateWiki() error {
	var entries []DatasetEntry

	wikiTopics := []string{
		"STEM",
		"Scientific method",
		"Encyclopedia",
		"Knowledge representation and reasoning",
		"Machine learning",
	}
	for _, topic := range wikiTopics {
		summary, sourceURL, err := FetchWikipediaSummary(topic)
		if err != nil {
			log.Printf("wiki topic fetch failed (%s): %v", topic, err)
			continue
		}
		entries = append(entries, DatasetEntry{
			Text:        fmt.Sprintf("%s: %s", topic, summary),
			Source:      "Wikipedia",
			Domain:      "Wikipedia/STEM",
			URL:         sourceURL,
			RetrievedAt: NowRFC3339(),
		})
	}

	corpTopics := []string{
		"Amazon Web Services",
		"Google Cloud Platform",
		"Microsoft Azure",
		"Cloud computing",
		"Kubernetes",
	}
	for _, topic := range corpTopics {
		summary, sourceURL, err := FetchWikipediaSummary(topic)
		if err != nil {
			log.Printf("corporate topic fetch failed (%s): %v", topic, err)
			continue
		}
		entries = append(entries, DatasetEntry{
			Text:        fmt.Sprintf("%s: %s", topic, summary),
			Source:      "Wikipedia",
			Domain:      "Corporate/Cloud",
			URL:         sourceURL,
			RetrievedAt: NowRFC3339(),
		})
	}

	if len(entries) == 0 {
		return fmt.Errorf("phase3 corporate/wiki produced no entries")
	}

	return WriteJSONL(getPhaseFile("phase3_wiki"), entries)
}
