#!/usr/bin/env python3
"""
Convert text to binary trit format (.t3b) with dual packing support

Packing schemes (auto-selected based on trit dimension):
- TritPack5: 5 trits per byte (optimal for dim <= 20, 95% byte capacity)
- Trit20: 20 trits per 32-bit word (optimal for dim > 20, 500% vs uncompressed)

Formats:
- v1: Legacy 4-trit per byte (backward compatible)
- v2: Dual packing with auto-selection header

Usage:
    python text_to_trit_binary.py <input.jsonl> <output.t3b> [max_samples] [--packing auto|t5|t20]
"""

import struct
import sys
import json
import argparse
from typing import List, Tuple

# Precomputed powers of 3 for Trit20 encoding
POW3_TABLE = [3**i for i in range(21)]  # 3^0 through 3^20

def text_to_trits(text: str, dim: int = 256) -> List[int]:
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


def trit_to_unbalanced(t: int) -> int:
    """Convert balanced trit {-1, 0, +1} to unbalanced {0, 1, 2}"""
    return t + 1


def unbalanced_to_trit(u: int) -> int:
    """Convert unbalanced {0, 1, 2} to balanced trit {-1, 0, +1}"""
    return u - 1


def pack_trits_t5(trits: List[int]) -> bytes:
    """
    TritPack5: Pack 5 trits per byte (2 bits each)
    5 trits = 243 states, fits in 256 byte states (95% capacity)
    """
    packed = bytearray()
    for i in range(0, len(trits), 5):
        byte = 0
        for j in range(5):
            if i + j < len(trits):
                u = trit_to_unbalanced(trits[i + j])
                byte |= (u << (j * 2))  # 2 bits per trit
        packed.append(byte)
    return bytes(packed)


def unpack_trits_t5(packed: bytes, length: int) -> List[int]:
    """Unpack TritPack5 bytes back to trits"""
    trits = []
    for byte in packed:
        for j in range(5):
            if len(trits) >= length:
                break
            u = (byte >> (j * 2)) & 0x3
            trits.append(unbalanced_to_trit(u))
    return trits


def pack_trits_t20(trits: List[int]) -> bytes:
    """
    Trit20: Pack 20 trits per 32-bit word
    Polynomial encoding: Σ(trit_unbalanced[i] × 3^i)
    Maximum: 3^20 - 1 = 3,486,784,401 (fits in uint32)
    """
    packed = bytearray()
    for i in range(0, len(trits), 20):
        word = 0
        for j in range(20):
            if i + j < len(trits):
                u = trit_to_unbalanced(trits[i + j])
                word += u * POW3_TABLE[j]
        packed.extend(struct.pack('<I', word))
    return bytes(packed)


def unpack_trits_t20(packed: bytes, length: int) -> List[int]:
    """Unpack Trit20 32-bit words back to trits"""
    trits = []
    for i in range(0, len(packed), 4):
        word = struct.unpack('<I', packed[i:i+4])[0]
        for j in range(20):
            if len(trits) >= length:
                break
            u = word % 3
            trits.append(unbalanced_to_trit(u))
            word //= 3
    return trits


def pack_trits_legacy4(trits: List[int]) -> bytes:
    """Legacy v1: Pack 4 trits per byte (2 bits each)"""
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


def select_packing(dim: int, preference: str = "auto") -> str:
    """Auto-select optimal packing scheme"""
    if preference == "t5":
        return "t5"
    if preference == "t20":
        return "t20"
    # Auto: T5 for small dimensions (<=20), T20 for larger
    return "t5" if dim <= 20 else "t20"


def convert_jsonl_to_t3b(
    input_path: str,
    output_path: str,
    dim: int = 256,
    max_samples: int = None,
    packing: str = "auto",
    version: int = 2
) -> int:
    """Convert JSONL text file to binary trit format (.t3b)"""
    
    # Determine packing scheme
    scheme = select_packing(dim, packing)
    
    samples = []
    count = 0
    
    print(f"Reading {input_path}...")
    print(f"  Trit dimension: {dim}")
    print(f"  Packing scheme: {scheme} ({'auto-selected' if packing == 'auto' else 'manual'})")
    print(f"  Format version: {version}")
    
    with open(input_path, 'r', encoding='utf-8') as f:
        for line in f:
            try:
                data = json.loads(line)
                text = data.get('text', '')
                if len(text) >= 50:  # Minimum length filter
                    trits = text_to_trits(text, dim)
                    
                    # Pack based on selected scheme
                    if scheme == "t5":
                        packed = pack_trits_t5(trits)
                    elif scheme == "t20":
                        packed = pack_trits_t20(trits)
                    else:
                        packed = pack_trits_legacy4(trits)
                    
                    samples.append((len(trits), packed))
                    count += 1
                    
                    if max_samples and count >= max_samples:
                        break
                    if count % 10000 == 0:
                        print(f"  Processed {count} samples...")
            except Exception as e:
                continue
    
    print(f"Total samples: {count}")
    print(f"Writing {output_path}...")
    
    # Map scheme to header code
    scheme_code = {"t5": 1, "t20": 2}.get(scheme, 0)
    
    # Write header and samples
    with open(output_path, 'wb') as f:
        if version == 2:
            # v2 header: 24 bytes
            # magic (4) + version (2) + scheme (1) + reserved (1) + 
            # sample_count (4) + dim (4) + max_trits (4) = 20 bytes
            f.write(b'T3B2')
            f.write(struct.pack('<H', 2))  # version
            f.write(struct.pack('<B', scheme_code))  # packing scheme
            f.write(struct.pack('<B', 0))  # reserved
            f.write(struct.pack('<I', count))  # sample count
            f.write(struct.pack('<I', dim))  # trit dimension
            f.write(struct.pack('<I', dim))  # max trits per sample
        else:
            # v1 legacy header: 20 bytes
            f.write(b'T3B\x00')
            f.write(struct.pack('<I', 1))  # version
            f.write(struct.pack('<Q', count))  # sample count
            f.write(struct.pack('<I', dim))  # trit dimension
        
        # Write each sample
        for length, packed in samples:
            if version == 1:
                f.write(struct.pack('<I', length))  # per-sample length
            f.write(packed)
    
    # Calculate sizes
    import os
    text_size = os.path.getsize(input_path)
    bin_size = os.path.getsize(output_path)
    ratio = text_size / bin_size if bin_size > 0 else 0
    
    # Calculate theoretical sizes
    if scheme == "t5":
        packed_per_sample = (dim + 4) // 5
    elif scheme == "t20":
        packed_per_sample = ((dim + 19) // 20) * 4
    else:
        packed_per_sample = (dim + 3) // 4
    
    print(f"\nDone!")
    print(f"Text size: {text_size / 1024 / 1024:.2f} MB")
    print(f"Binary size: {bin_size / 1024 / 1024:.2f} MB")
    print(f"Compression ratio: {ratio:.2f}x")
    print(f"Samples: {count}")
    print(f"Packed size per sample: {packed_per_sample} bytes ({dim} trits)")
    print(f"Bits per trit: {packed_per_sample * 8 / dim:.2f}")
    
    return count


def main():
    parser = argparse.ArgumentParser(
        description="Convert JSONL text to binary trit format (.t3b)"
    )
    parser.add_argument("input", help="Input JSONL file")
    parser.add_argument("output", help="Output .t3b file")
    parser.add_argument("--max-samples", type=int, default=None,
                        help="Maximum samples to process")
    parser.add_argument("--dim", type=int, default=256,
                        help="Trit dimension per sample (default: 256)")
    parser.add_argument("--packing", choices=["auto", "t5", "t20"], default="auto",
                        help="Packing scheme (default: auto)")
    parser.add_argument("--version", type=int, choices=[1, 2], default=2,
                        help="Format version (default: 2)")
    
    args = parser.parse_args()
    
    convert_jsonl_to_t3b(
        args.input,
        args.output,
        dim=args.dim,
        max_samples=args.max_samples,
        packing=args.packing,
        version=args.version
    )


if __name__ == "__main__":
    main()
