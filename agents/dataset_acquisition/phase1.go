package main

import (
	"encoding/json"
	"fmt"
	"log"
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
		for word, definition := range dict {
			if len(definition) > 0 {
				entries = append(entries, DatasetEntry{
					Text:   fmt.Sprintf("%s: %s", strings.Title(word), definition),
					Source: "WebstersEnglishDictionary",
					Domain: "NLP/Dictionary",
				})
			}
		}
	} else {
		log.Printf("Warning: unmarshal dictionary failed: %v", err)
	}

	// Add standard math axioms and branches (mock representation of a larger math corpus)
	mathTexts := []string{
		"Algebra is the study of mathematical symbols and the rules for manipulating these symbols.",
		"Calculus is the mathematical study of continuous change.",
		"Geometry is a branch of mathematics concerned with questions of shape, size, relative position of figures, and the properties of space.",
	}
	for _, text := range mathTexts {
		entries = append(entries, DatasetEntry{
			Text:   text,
			Source: "MathFoundations",
			Domain: "Mathematics",
		})
	}

	return WriteJSONL(getPhaseFile("phase1_nlp"), entries)
}

// RunPhase1Programming handles C++, R, and Go documentation / snippets
func RunPhase1Programming() error {
	var entries []DatasetEntry

	// Go documentation basics
	goDocs := []string{
		"Go is an open source programming language that makes it easy to build simple, reliable, and efficient software.",
		"A goroutine is a lightweight thread managed by the Go runtime.",
		"Channels are a typed conduit through which you can send and receive values with the channel operator, <-.",
	}
	
	// C++ basics
	cppDocs := []string{
		"C++ is a general-purpose programming language created by Bjarne Stroustrup as an extension of the C programming language.",
		"Templates are the foundation of generic programming in C++, which involves writing code in a way that is independent of any particular type.",
		"RAII (Resource Acquisition Is Initialization) is a C++ programming technique which binds the life cycle of a resource to the lifetime of an object.",
	}

	// R basics
	rDocs := []string{
		"R is a programming language for statistical computing and graphics.",
		"Data frames are fundamental data structures in R, used for storing data tables.",
		"ggplot2 is a data visualization package for the statistical programming language R.",
	}

	for _, text := range goDocs {
		entries = append(entries, DatasetEntry{Text: text, Source: "GoDocs", Domain: "Programming/Go"})
	}
	for _, text := range cppDocs {
		entries = append(entries, DatasetEntry{Text: text, Source: "CppDocs", Domain: "Programming/C++"})
	}
	for _, text := range rDocs {
		entries = append(entries, DatasetEntry{Text: text, Source: "RDocs", Domain: "Programming/R"})
	}

	return WriteJSONL(getPhaseFile("phase1_prog"), entries)
}
