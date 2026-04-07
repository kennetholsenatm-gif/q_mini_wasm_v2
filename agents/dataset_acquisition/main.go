package main

import (
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"time"
)

const MaxDiskUsage = 150 * 1024 * 1024 * 1024 // 150 GB safety limit

var (
	RunRoot   = getenvDefault("QMINI_RUN_ROOT", `D:\qmini_ff_prod`)
	TargetDir = filepath.Join(RunRoot, "datasets", "raw")
	LinkPath  = filepath.Join(RunRoot, "datasets", "processed", "data.jsonl")
	ReportPath = filepath.Join(RunRoot, "reports", "dataset_manifest.json")
)

type phaseResult struct {
	Name       string  `json:"name"`
	File       string  `json:"file"`
	Entries    int     `json:"entries"`
	SizeBytes  int64   `json:"size_bytes"`
	SHA256     string  `json:"sha256"`
	DurationS  float64 `json:"duration_s"`
}

func getenvDefault(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

func getPhaseFile(phase string) string {
	return filepath.Join(TargetDir, phase+".jsonl")
}

func executePhase(name, phaseFile string, fn func() error) (phaseResult, error) {
	start := time.Now()
	if err := fn(); err != nil {
		return phaseResult{}, err
	}

	entries, err := CountJSONLLines(phaseFile)
	if err != nil {
		return phaseResult{}, fmt.Errorf("failed counting entries in %s: %w", phaseFile, err)
	}
	if entries == 0 {
		return phaseResult{}, fmt.Errorf("phase %s produced zero entries", name)
	}

	sizeBytes, err := FileSize(phaseFile)
	if err != nil {
		return phaseResult{}, fmt.Errorf("failed reading phase file size %s: %w", phaseFile, err)
	}
	sha, err := FileSHA256(phaseFile)
	if err != nil {
		return phaseResult{}, fmt.Errorf("failed hashing phase file %s: %w", phaseFile, err)
	}

	return phaseResult{
		Name:      name,
		File:      phaseFile,
		Entries:   entries,
		SizeBytes: sizeBytes,
		SHA256:    sha,
		DurationS: time.Since(start).Seconds(),
	}, nil
}

func main() {
	log.Printf("Starting Dataset Acquisition Pipeline (run_root=%s)...", RunRoot)

	if err := EnsureDir(RunRoot); err != nil {
		log.Fatalf("Failed to create run root: %v", err)
	}

	// 1. Setup Directories
	if err := EnsureDir(TargetDir); err != nil {
		log.Fatalf("Failed to create target directory: %v", err)
	}
	if err := EnsureDir(filepath.Dir(LinkPath)); err != nil {
		log.Fatalf("Failed to create processed dataset directory: %v", err)
	}
	if err := EnsureDir(filepath.Dir(ReportPath)); err != nil {
		log.Fatalf("Failed to create report directory: %v", err)
	}

	// 2. Initialize Size Tracker
	if err := CalculateDirSize(RunRoot); err != nil {
		log.Printf("Warning: failed to calculate initial dir size: %v", err)
	}

	if err := InitFetchTransport(); err != nil {
		log.Fatalf("Failed to initialize MCP-backed fetch transport: %v", err)
	}
	defer ShutdownFetchTransport()

	phases := []struct {
		Name string
		File string
		Fn   func() error
	}{
		{Name: "phase1_nlp_math", File: getPhaseFile("phase1_nlp"), Fn: RunPhase1NLPMath},
		{Name: "phase1_programming", File: getPhaseFile("phase1_prog"), Fn: RunPhase1Programming},
		{Name: "phase2_stem_social", File: getPhaseFile("phase2_stem"), Fn: RunPhase2STEM},
		{Name: "phase2_quantum", File: getPhaseFile("phase2_quantum"), Fn: RunPhase2Quantum},
		{Name: "phase3_corporate_wiki", File: getPhaseFile("phase3_wiki"), Fn: RunPhase3CorporateWiki},
		{Name: "phase4_academia", File: getPhaseFile("phase4_acad"), Fn: RunPhase4Academia},
	}

	results := make([]phaseResult, 0, len(phases))
	for _, phase := range phases {
		log.Printf("Running %s...", phase.Name)
		result, err := executePhase(phase.Name, phase.File, phase.Fn)
		if err != nil {
			log.Fatalf("%s failed: %v", phase.Name, err)
		}
		results = append(results, result)
		log.Printf("Completed %s: entries=%d size=%d sha=%s", result.Name, result.Entries, result.SizeBytes, result.SHA256)
	}

	// Final Step: Validation and Linking
	log.Println("Validating and linking dataset...")
	if err := ConsolidateAndLink(); err != nil {
		log.Fatalf("Failed to consolidate and link dataset: %v", err)
	}

	combinedPath := filepath.Join(TargetDir, "combined.jsonl")
	combinedEntries, err := CountJSONLLines(combinedPath)
	if err != nil {
		log.Fatalf("Failed to count combined dataset lines: %v", err)
	}
	combinedSize, err := FileSize(combinedPath)
	if err != nil {
		log.Fatalf("Failed to stat combined dataset: %v", err)
	}
	combinedSHA, err := FileSHA256(combinedPath)
	if err != nil {
		log.Fatalf("Failed to hash combined dataset: %v", err)
	}

	manifest := map[string]interface{}{
		"generated_at":      NowRFC3339(),
		"run_root":          RunRoot,
		"target_dir":        TargetDir,
		"combined_path":     combinedPath,
		"link_path":         LinkPath,
		"combined_entries":  combinedEntries,
		"combined_size_bytes": combinedSize,
		"combined_sha256":   combinedSHA,
		"phases":            results,
	}
	if err := WriteJSONFile(ReportPath, manifest); err != nil {
		log.Fatalf("Failed to write dataset manifest: %v", err)
	}

	log.Printf("Dataset Acquisition Complete! manifest=%s", ReportPath)
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
			return fmt.Errorf("missing required phase file: %s", srcPath)
		}

		count, err := CountJSONLLines(srcPath)
		if err != nil {
			return fmt.Errorf("failed counting phase file %s: %w", srcPath, err)
		}
		if count == 0 {
			return fmt.Errorf("phase file %s is empty", srcPath)
		}
		
		in, err := os.Open(srcPath)
		if err != nil {
			log.Printf("Failed to open %s: %v", srcPath, err)
			return err
		}
		
		_, err = io.Copy(out, in)
		in.Close()
		if err != nil {
			log.Printf("Failed to copy %s: %v", srcPath, err)
			return err
		}
	}

	// Ensure target directory exists for link
	if err := EnsureDir(filepath.Dir(LinkPath)); err != nil {
		return err
	}

	// Perform copy instead of symlink to avoid Windows privilege issues
	if err := CopyFile(finalFile, LinkPath); err != nil {
		return err
	}

	combinedCount, err := CountJSONLLines(finalFile)
	if err != nil {
		return err
	}
	if combinedCount == 0 {
		return fmt.Errorf("combined dataset is empty")
	}

	return nil
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
