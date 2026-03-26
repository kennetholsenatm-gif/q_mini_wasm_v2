"""On-disk TPEM sidecar bundle (magic header + SHA-256 + packed payload).

Specification: ``docs/TPEM_ARTIFACT_FORMAT.md`` in the repository root.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Union

from .trit_pack import PACK_ENCODING_VERSION

MAGIC = b"QMWTPEM1"
HEADER_STRUCT = struct.Struct("<8sIIQ32s")
BUNDLE_FORMAT_VERSION = 1
HEADER_SIZE = HEADER_STRUCT.size  # 56


@dataclass(frozen=True)
class TpemBundle:
    """Parsed TPEM sidecar (header + raw packed bytes)."""

    bundle_format_version: int
    pack_encoding_version: int
    payload: bytes
    payload_sha256: bytes

    @property
    def payload_byte_length(self) -> int:
        return len(self.payload)


def write_tpem_bundle(
    path: Union[str, Path],
    payload: bytes,
    *,
    bundle_format_version: int = BUNDLE_FORMAT_VERSION,
    pack_encoding_version: int | None = None,
) -> None:
    """Write a TPEM bundle file (creates parent directories)."""
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    pe = int(PACK_ENCODING_VERSION if pack_encoding_version is None else pack_encoding_version)
    digest = hashlib.sha256(payload).digest()
    header = HEADER_STRUCT.pack(
        MAGIC,
        int(bundle_format_version),
        pe,
        len(payload),
        digest,
    )
    p.write_bytes(header + payload)


def read_tpem_bundle(path: Union[str, Path]) -> TpemBundle:
    """Read and validate a TPEM bundle; raises ValueError on corruption."""
    raw = Path(path).read_bytes()
    if len(raw) < HEADER_SIZE:
        raise ValueError("TPEM bundle too small for header")
    magic, bfv, pev, declared_len, digest = HEADER_STRUCT.unpack_from(raw, 0)
    if magic != MAGIC:
        raise ValueError(f"bad TPEM magic: {magic!r}")
    payload = raw[HEADER_SIZE:]
    if len(payload) != declared_len:
        raise ValueError(
            f"payload length mismatch: file has {len(payload)} bytes, "
            f"header declares {declared_len}"
        )
    calc = hashlib.sha256(payload).digest()
    if calc != digest:
        raise ValueError("payload_sha256 mismatch")
    return TpemBundle(
        bundle_format_version=bfv,
        pack_encoding_version=pev,
        payload=payload,
        payload_sha256=digest,
    )


def verify_tpem_bundle(path: Union[str, Path]) -> int:
    """CLI helper: load bundle and return 0 on success."""
    try:
        b = read_tpem_bundle(path)
        print(
            f"OK bundle_format={b.bundle_format_version} "
            f"pack_encoding={b.pack_encoding_version} payload_bytes={len(b.payload)}"
        )
        return 0
    except ValueError as e:
        print(f"VERIFY_FAIL {path}: {e}", file=sys.stderr)
        return 1


def main() -> None:
    ap = argparse.ArgumentParser(description="TPEM bundle utilities")
    ap.add_argument("command", choices=["verify"], help="verify: validate bundle file")
    ap.add_argument("path", type=Path, help="Path to .tpem file")
    args = ap.parse_args()
    if args.command == "verify":
        sys.exit(verify_tpem_bundle(args.path))


if __name__ == "__main__":
    main()
