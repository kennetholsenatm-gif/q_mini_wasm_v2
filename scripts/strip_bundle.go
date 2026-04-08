package main

import (
	"crypto/sha256"
	"encoding/binary"
	"fmt"
	"os"
)

const embeddedAssetFooterMagic = "QMINIWASM_WUI_ASSET_FOOTER_V1"

func main() {
	exePath := `c:\GitHub\q_mini_wasm_v2\qminiwasm.exe`

	stat, err := os.Stat(exePath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	footerLen := int64(len(embeddedAssetFooterMagic) + 8 + sha256.Size)

	if stat.Size() < footerLen {
		fmt.Println("No embedded bundle found (file too small)")
		return
	}

	f, err := os.Open(exePath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error opening: %v\n", err)
		os.Exit(1)
	}
	defer f.Close()

	footer := make([]byte, footerLen)
	_, err = f.ReadAt(footer, stat.Size()-footerLen)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error reading footer: %v\n", err)
		os.Exit(1)
	}

	if string(footer[:len(embeddedAssetFooterMagic)]) != embeddedAssetFooterMagic {
		fmt.Println("No embedded bundle found (footer magic not found)")
		return
	}

	size := binary.LittleEndian.Uint64(footer[len(embeddedAssetFooterMagic) : len(embeddedAssetFooterMagic)+8])
	offset := stat.Size() - footerLen - int64(size)

	fmt.Printf("Found embedded bundle:\n")
	fmt.Printf("  Bundle offset: %d\n", offset)
	fmt.Printf("  Bundle size: %d bytes\n", size)
	fmt.Printf("  Footer size: %d bytes\n", footerLen)
	fmt.Printf("  Trimming to: %d bytes\n", offset)

	// Close file before truncating
	f.Close()

	// Truncate
	err = os.Truncate(exePath, offset)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error truncating: %v\n", err)
		os.Exit(1)
	}

	fmt.Println("\nOld embedded bundle stripped successfully!")
	fmt.Printf("New file size: %d bytes\n", offset)
}
