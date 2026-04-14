#!/usr/bin/env python3
"""
Convert text to binary trit format (TERNARY encoding)
Each trit: -1, 0, +1 stored in 2 bits (4 trits per byte)
Format: .t3b (Trit Binary)

Header (16 bytes):
- Magic: "T3B\x00" (4 bytes)
- Version: 1 (4 bytes)
- Sample count (8 bytes)

Each sample:
- Length in trits (4 bytes)
- Trit data (packed 4 trits per byte)
"""

import struct
import sys
import json

def text_to_trits(text, dim=256):
    """Convert text to ternary trits using position-sensitive hashing"""
    trits = []
    for i, char in enumerate(text[:dim]):
        # Position-sensitive hash: combine char value with position
        hash_val = (ord(char) * 2654435761 + i * 2246822519) & 0xFFFFFFFF
        # Map to ternary: -1, 0, +1 based on hash
        val = hash_val % 3
        if val == 0:
            trits.append(-1)
        elif val == 1:
            trits.append(0)
        else:
            trits.append(1)
    # Pad to dim
    while len(trits) < dim:
        trits.append(0)
    return trits[:dim]

def pack_trits(trits):
    """Pack trits into bytes (4 trits per byte, 2 bits each)"""
    # Trit encoding: -1 -> 00, 0 -> 01, +1 -> 10
    encoding = {-1: 0b00, 0: 0b01, 1: 0b10}
    packed = bytearray()
    for i in range(0, len(trits), 4):
        byte = 0
        for j in range(4):
            if i + j < len(trits):
                t = trits[i + j]
                byte |= (encoding.get(t, 0b00) << (j * 2))
        packed.append(byte)
    return bytes(packed)

def unpack_trits(packed, length):
    """Unpack bytes back to trits"""
    decoding = {0b00: -1, 0b01: 0, 0b10: 1, 0b11: 0}  # 0b11 unused -> 0
    trits = []
    for byte in packed:
        for j in range(4):
            if len(trits) >= length:
                break
            t = (byte >> (j * 2)) & 0b11
            trits.append(decoding.get(t, 0))
    return trits

def convert_jsonl_to_t3b(input_path, output_path, dim=256, max_samples=None):
    """Convert JSONL text file to binary trit format (.t3b)"""
    samples = []
    count = 0
    
    print(f"Reading {input_path}...")
    with open(input_path, 'r', encoding='utf-8') as f:
        for line in f:
            try:
                data = json.loads(line)
                text = data.get('text', '')
                if len(text) >= 100:  # Minimum length filter
                    trits = text_to_trits(text, dim)
                    packed = pack_trits(trits)
                    samples.append((len(trits), packed))
                    count += 1
                    if max_samples and count >= max_samples:
                        break
                    if count % 10000 == 0:
                        print(f"  Processed {count} samples...")
            except:
                continue
    
    print(f"Total samples: {count}")
    print(f"Writing {output_path}...")
    
    # Write header and samples
    with open(output_path, 'wb') as f:
        # Header: magic (4) + version (4) + sample_count (8) + dim (4) = 20 bytes
        f.write(b'T3B\x00')
        f.write(struct.pack('<I', 1))  # version
        f.write(struct.pack('<Q', count))  # sample count
        f.write(struct.pack('<I', dim))  # trit dimension
        
        # Write each sample
        for length, packed in samples:
            f.write(struct.pack('<I', length))  # trit count
            f.write(packed)  # packed trit data
    
    # Calculate sizes
    import os
    text_size = os.path.getsize(input_path)
    bin_size = os.path.getsize(output_path)
    ratio = text_size / bin_size if bin_size > 0 else 0
    
    print(f"\nDone!")
    print(f"Text size: {text_size / 1024 / 1024:.2f} MB")
    print(f"Binary size: {bin_size / 1024 / 1024:.2f} MB")
    print(f"Compression ratio: {ratio:.2f}x")
    print(f"Samples: {count}")
    
    return count

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python text_to_trit_binary.py <input.jsonl> <output.t3b> [max_samples]")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    max_samples = int(sys.argv[3]) if len(sys.argv) > 3 else None
    
    convert_jsonl_to_t3b(input_file, output_file, dim=256, max_samples=max_samples)
