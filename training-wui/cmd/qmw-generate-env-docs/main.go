// Command qmw-generate-env-docs regenerates docs/environment-variables.md from .env.schema.
package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/kennetholsenatm-gif/qminiwasm-core/training-wui/envdocs"
)

func main() {
	root := flag.String("root", ".", "repository root (directory containing .env.schema)")
	flag.Parse()
	if err := envdocs.WriteFromRepoRoot(*root); err != nil {
		fmt.Fprintf(os.Stderr, "qmw-generate-env-docs: %v\n", err)
		os.Exit(1)
	}
	fmt.Println("wrote docs/environment-variables.md")
}
