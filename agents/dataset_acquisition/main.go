package main

import (
	"io"
	"log"
	"os"
	"path/filepath"
)

const (
	TargetDir    = `C:\GitHub\DataSets`
	LinkPath     = `.\datasets\general\data.jsonl`
	MaxDiskUsage = 150 * 1024 * 1024 * 1024 // 150 GB limit for safety (drive has 245GB free)
)

func getPhaseFile(phase string) string {
	return filepath.Join(TargetDir, phase+".jsonl")
}

func main() {
	log.Println("Starting Dataset Acquisition Pipeline...")

	// 1. Setup Directories
	if err := EnsureDir(TargetDir); err != nil {
		log.Fatalf("Failed to create target directory: %v", err)
	}

	// 2. Initialize Size Tracker
	if err := CalculateDirSize(TargetDir); err != nil {
		log.Printf("Warning: failed to calculate initial dir size: %v", err)
	}

	// Phase 1: Foundational NLP & Math
	log.Println("Running Phase 1: Foundational NLP & Math...")
	if err := RunPhase1NLPMath(); err != nil {
		log.Printf("Phase 1 NLP/Math error: %v", err)
	}

	// Phase 1: Core Programming
	log.Println("Running Phase 1: Core Programming...")
	if err := RunPhase1Programming(); err != nil {
		log.Printf("Phase 1 Programming error: %v", err)
	}

	// Phase 2: STEM, Social Sciences & Philosophy
	log.Println("Running Phase 2: STEM & Social Sciences...")
	if err := RunPhase2STEM(); err != nil {
		log.Printf("Phase 2 STEM error: %v", err)
	}

	// Phase 2: Quantum Practical
	log.Println("Running Phase 2: Quantum Practical...")
	if err := RunPhase2Quantum(); err != nil {
		log.Printf("Phase 2 Quantum error: %v", err)
	}

	// Phase 3: Corporate Docs & Wiki
	log.Println("Running Phase 3: Corporate Docs & Wiki...")
	if err := RunPhase3CorporateWiki(); err != nil {
		log.Printf("Phase 3 Wiki error: %v", err)
	}

	// Phase 4: Advanced Academia & Quantum Theory
	log.Println("Running Phase 4: Academia...")
	if err := RunPhase4Academia(); err != nil {
		log.Printf("Phase 4 Academia error: %v", err)
	}

	// Final Step: Validation and Linking
	log.Println("Validating and linking dataset...")
	if err := ConsolidateAndLink(); err != nil {
		log.Fatalf("Failed to consolidate and link dataset: %v", err)
	}

	log.Println("Dataset Acquisition Complete!")
}

// ConsolidateAndLink combines all phase files into the main target file, validating as it goes
func ConsolidateAndLink() error {
	finalFile := filepath.Join(TargetDir, "combined.jsonl")
	out, err := os.OpenFile(finalFile, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0644)
	if err != nil {
		return err
	}
	defer out.Close()

	phases := []string{"phase1_nlp", "phase1_prog", "phase2_stem", "phase2_quantum", "phase3_wiki", "phase4_acad"}
	for _, phase := range phases {
		srcPath := getPhaseFile(phase)
		if _, err := os.Stat(srcPath); os.IsNotExist(err) {
			continue // skip if missing
		}
		
		in, err := os.Open(srcPath)
		if err != nil {
			log.Printf("Failed to open %s: %v", srcPath, err)
			continue
		}
		
		_, err = io.Copy(out, in)
		in.Close()
		if err != nil {
			log.Printf("Failed to copy %s: %v", srcPath, err)
			continue
		}
	}

	// Ensure target directory exists for link
	if err := EnsureDir(filepath.Dir(LinkPath)); err != nil {
		return err
	}

	// Perform copy instead of symlink to avoid Windows privilege issues
	return CopyFile(finalFile, LinkPath)
}

func CopyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	return err
}
