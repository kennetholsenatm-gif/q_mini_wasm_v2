package edgeartifacts

import (
	"testing"
)

func TestPackTernaryList_firstBlockMatchesPython(t *testing.T) {
	block := []int{-1, 0, 1, 1, 0}
	b, err := PackTernaryList(block)
	if err != nil {
		t.Fatal(err)
	}
	if len(b) != 1 || b[0] != 52 {
		t.Fatalf("first packed byte = %v want single byte 52 (MSB-first 5-trit group)", b)
	}
}

func TestSyntheticPackedPayload_length(t *testing.T) {
	p, err := SyntheticPackedPayload()
	if err != nil {
		t.Fatal(err)
	}
	if len(p) != 127 {
		t.Fatalf("synthetic payload len %d want 127 (635 trits / 5)", len(p))
	}
}

func TestTpemBundleRoundTrip(t *testing.T) {
	payload := []byte{1, 2, 3, 4, 5}
	path := t.TempDir() + "/w.tpem"
	if err := WriteTpemBundle(path, payload, PACK_ENCODING_VERSION); err != nil {
		t.Fatal(err)
	}
	b, err := ReadTpemBundle(path)
	if err != nil {
		t.Fatal(err)
	}
	if string(b.Payload) != string(payload) {
		t.Fatalf("payload mismatch")
	}
	if b.PackEncodingVersion != PACK_ENCODING_VERSION {
		t.Fatalf("pack version %d", b.PackEncodingVersion)
	}
}
