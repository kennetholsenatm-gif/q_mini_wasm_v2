package edgeartifacts

import (
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"fmt"
	"os"
)

var (
	// TpemMagic is the on-disk TPEM sidecar magic (docs/TPEM_ARTIFACT_FORMAT.md).
	TpemMagic = []byte("QMWTPEM1")
	// BundleFormatVersion is the container header version.
	BundleFormatVersion uint32 = 1
)

const tpemHeaderSize = 8 + 4 + 4 + 8 + 32

// WriteTpemBundle writes magic + header + payload (canonical SHA-256 over payload).
func WriteTpemBundle(path string, payload []byte, packEncodingVersion uint32) error {
	if len(TpemMagic) != 8 {
		return errors.New("invalid magic length")
	}
	digest := sha256.Sum256(payload)
	buf := bytes.NewBuffer(make([]byte, 0, tpemHeaderSize+len(payload)))
	_, _ = buf.Write(TpemMagic)
	_ = binary.Write(buf, binary.LittleEndian, BundleFormatVersion)
	_ = binary.Write(buf, binary.LittleEndian, packEncodingVersion)
	_ = binary.Write(buf, binary.LittleEndian, uint64(len(payload)))
	_, _ = buf.Write(digest[:])
	_, _ = buf.Write(payload)
	return os.WriteFile(path, buf.Bytes(), 0o644)
}

// TpemBundle is a parsed sidecar (header + payload).
type TpemBundle struct {
	BundleFormatVersion uint32
	PackEncodingVersion uint32
	Payload             []byte
	PayloadSHA256       [32]byte
}

// ReadTpemBundle reads and validates a .tpem file.
func ReadTpemBundle(path string) (*TpemBundle, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	if len(raw) < tpemHeaderSize {
		return nil, errors.New("TPEM bundle too small for header")
	}
	if !bytes.Equal(raw[0:8], TpemMagic) {
		return nil, fmt.Errorf("bad TPEM magic: %q", raw[0:8])
	}
	bfv := binary.LittleEndian.Uint32(raw[8:12])
	pev := binary.LittleEndian.Uint32(raw[12:16])
	declared := binary.LittleEndian.Uint64(raw[16:24])
	var digest [32]byte
	copy(digest[:], raw[24:56])
	payload := raw[tpemHeaderSize:]
	if uint64(len(payload)) != declared {
		return nil, fmt.Errorf("payload length mismatch: file has %d bytes, header declares %d", len(payload), declared)
	}
	sum := sha256.Sum256(payload)
	if sum != digest {
		return nil, errors.New("payload_sha256 mismatch")
	}
	return &TpemBundle{
		BundleFormatVersion: bfv,
		PackEncodingVersion: pev,
		Payload:             payload,
		PayloadSHA256:       sum,
	}, nil
}
