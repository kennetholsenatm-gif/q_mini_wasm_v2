// Command qmw-build-edge-artifacts builds dist/edge-artifacts (wasm + tpem + manifest) without Python for the main path.
package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/edgeartifacts"
)

func main() {
	repoRoot := flag.String("repo-root", "", "repository root (contains corpus/, scripts/); default: parent of training-wui if set, else cwd")
	outDir := flag.String("out-dir", "", "output directory (required for build)")
	tier := flag.Int("tier", 2, "enclave tier 1–5")
	wasmOpt := flag.String("wasm-opt-level", "O3", "wasm-opt pass for tier 1 only")
	checkpoint := flag.String("checkpoint", "", "optional .pt checkpoint (else synthetic weights)")
	python := flag.String("python", "python", "Python for checkpoint extraction only")
	verifyTpem := flag.String("verify-tpem", "", "validate a .tpem file and exit")
	verifyContract := flag.String("verify-contract", "", "validate artifact directory (artifact_manifest.json) and exit")
	flag.Parse()

	root := *repoRoot
	if root == "" {
		wd, err := os.Getwd()
		if err != nil {
			fmt.Fprintf(os.Stderr, "qmw-build-edge-artifacts: %v\n", err)
			os.Exit(1)
		}
		root = wd
	}

	if *verifyTpem != "" {
		b, err := edgeartifacts.ReadTpemBundle(*verifyTpem)
		if err != nil {
			fmt.Fprintf(os.Stderr, "VERIFY_FAIL %s: %v\n", *verifyTpem, err)
			os.Exit(1)
		}
		fmt.Printf(
			"OK bundle_format=%d pack_encoding=%d payload_bytes=%d\n",
			b.BundleFormatVersion, b.PackEncodingVersion, len(b.Payload),
		)
		return
	}
	if *verifyContract != "" {
		if err := edgeartifacts.VerifyArtifactContract(*verifyContract); err != nil {
			fmt.Fprintf(os.Stderr, "ARTIFACT_CONTRACT_FAIL: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("OK artifact-contract %s\n", *verifyContract)
		return
	}

	if *outDir == "" {
		fmt.Fprintln(os.Stderr, "qmw-build-edge-artifacts: --out-dir is required for build")
		os.Exit(2)
	}

	log, err := edgeartifacts.Build(edgeartifacts.BuildOptions{
		RepoRoot:       root,
		OutDir:         *outDir,
		Tier:           *tier,
		WasmOptLevel:   *wasmOpt,
		CheckpointPath: *checkpoint,
		PythonExe:      *python,
	})
	if log.Len() > 0 {
		fmt.Print(log.String())
	}
	if err != nil {
		fmt.Fprintf(os.Stderr, "qmw-build-edge-artifacts: %v\n", err)
		os.Exit(1)
	}
}
