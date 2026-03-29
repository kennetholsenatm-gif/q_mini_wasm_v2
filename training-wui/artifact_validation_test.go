package main

import (
	"encoding/binary"
	"os"
	"path/filepath"
	"testing"
)

func TestValidateTpemInterchangeFile_minimalV2(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "x.pt")
	env := []byte(`{"format_version":2,"d_model":128,"io_d_model":128,"num_ternary_blocks":1}`)
	var buf []byte
	buf = append(buf, []byte(tpemInterchangeMagic)...)
	lenB := make([]byte, 8)
	binary.LittleEndian.PutUint64(lenB, uint64(len(env)))
	buf = append(buf, lenB...)
	buf = append(buf, env...)
	buf = append(buf, make([]byte, 16)...) // minimal non-empty tail (safetensors blob)
	if err := os.WriteFile(p, buf, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := ValidateTpemInterchangeFile(p); err != nil {
		t.Fatal(err)
	}
}

func TestValidateTpemInterchangeFile_badMagic(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "bad.pt")
	if err := os.WriteFile(p, []byte("NOTTPEM!!"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := ValidateTpemInterchangeFile(p); err == nil {
		t.Fatal("expected error")
	}
}
