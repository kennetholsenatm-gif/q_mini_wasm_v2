"""Save/load **trainable TPEM** (ternary-packed memory enclave) partitions for QMiniWASM.

Canonical location for trainable PyTorch artifacts. Full **WLES** dumps live under
:mod:`qminiwasm.wasm_host.memory_encode` and native hooks; both may coexist.

``qminiwasm.training.trainable_tpem`` re-exports this module for backward compatibility.
"""

from __future__ import annotations

import inspect
import json
import logging
import os
import tempfile
from pathlib import Path
from typing import Any, Dict, Optional, Union

import torch
import torch.nn as nn

logger = logging.getLogger(__name__)

TRAINABLE_TPEM_FORMAT_VERSION = 1
# Back-compat for external readers of this constant
CHECKPOINT_FORMAT_VERSION = TRAINABLE_TPEM_FORMAT_VERSION
D_MODEL = 4096
WLES_TRAINING_SIDE_VERSION = TRAINABLE_TPEM_FORMAT_VERSION

# Native C++ engine interchange: magic + JSON envelope + safetensors. See cpp/training/README.md.
TPEM_INTERCHANGE_MAGIC = b"QMWTPEM2"
TRAINABLE_TPEM_INTERCHANGE_VERSION = 2

__all__ = [
    "CHECKPOINT_FORMAT_VERSION",
    "D_MODEL",
    "TPEM_INTERCHANGE_MAGIC",
    "TRAINABLE_TPEM_FORMAT_VERSION",
    "TRAINABLE_TPEM_INTERCHANGE_VERSION",
    "WLES_TRAINING_SIDE_VERSION",
    "build_checkpoint_payload",
    "build_trainable_tpem_payload",
    "load_cascade_policy_from_checkpoint",
    "load_cascade_policy_from_trainable_tpem",
    "load_checkpoint_into_model",
    "load_trainable_tpem_into_model",
    "peek_trainable_tpem_geometry",
    "save_checkpoint",
    "save_trainable_tpem_artifact",
    "save_trainable_tpem_interchange_v2",
]


def _safetensors_torch():
    try:
        from safetensors.torch import load_file as st_load_file
        from safetensors.torch import save_file as st_save_file
    except ImportError as e:  # pragma: no cover
        raise ImportError(
            "The safetensors package is required for TPEM interchange v2. "
            "Install with: pip install safetensors"
        ) from e
    return st_load_file, st_save_file


def _flat_tensors_to_safetensors_bytes(flat: Dict[str, torch.Tensor]) -> bytes:
    _, st_save_file = _safetensors_torch()
    fd, tmpp = tempfile.mkstemp(suffix=".safetensors")
    os.close(fd)
    try:
        st_save_file(flat, tmpp)
        return Path(tmpp).read_bytes()
    finally:
        Path(tmpp).unlink(missing_ok=True)


def _safetensors_bytes_to_flat(blob: bytes, map_location: Any) -> Dict[str, torch.Tensor]:
    st_load_file, _ = _safetensors_torch()
    dev: Any = map_location if map_location is not None else "cpu"
    if not isinstance(dev, str):
        dev = str(dev)
    fd, tmpp = tempfile.mkstemp(suffix=".safetensors")
    os.close(fd)
    try:
        Path(tmpp).write_bytes(blob)
        return st_load_file(tmpp, device=dev)
    finally:
        Path(tmpp).unlink(missing_ok=True)


def save_trainable_tpem_interchange_v2(
    path: str | Path,
    model: Any,
    meta: Optional[Dict[str, Any]] = None,
) -> None:
    """Write trainable TPEM for the native LibTorch engine (interchange v2).

    Embeds ``quantum_router``, optional ``input_stem`` / ``output_head`` (when
    ``io_d_model != d_model``), and all ternary block tensors as F32 safetensors with
    prefixed keys. The native LibTorch trainer loads the same envelope and tensor keys
    for multi-block stacks and stem/head (see ``cpp/training/README.md``).
    """
    flat: Dict[str, torch.Tensor] = {}
    for k, v in model.quantum_router.state_dict().items():
        flat[f"quantum_router.{k}"] = v.detach().cpu().contiguous().to(torch.float32)
    blocks = getattr(model, "ternary_blocks", None)
    if blocks is not None:
        for i, blk in enumerate(blocks):
            for k, v in blk.state_dict().items():
                flat[f"ternary_blocks.{i}.{k}"] = v.detach().cpu().contiguous().to(torch.float32)
    else:
        for k, v in model.ternary_expert.state_dict().items():
            flat[f"ternary_expert.{k}"] = v.detach().cpu().contiguous().to(torch.float32)
    stem_mod = getattr(model, "input_stem", None)
    if isinstance(stem_mod, torch.nn.Linear):
        for k, v in stem_mod.state_dict().items():
            if isinstance(v, torch.Tensor):
                flat[f"input_stem.{k}"] = v.detach().cpu().contiguous().to(torch.float32)
    oh = getattr(model, "output_head", None)
    if oh is not None and hasattr(oh, "state_dict"):
        for k, v in oh.state_dict().items():
            if isinstance(v, torch.Tensor):
                flat[f"output_head.{k}"] = v.detach().cpu().contiguous().to(torch.float32)
    st_bytes = _flat_tensors_to_safetensors_bytes(flat)
    dm = int(getattr(model, "d_model", D_MODEL))
    nblk = int(getattr(model, "num_ternary_blocks", 1))
    iodm = int(getattr(model, "io_d_model", dm))
    env = {
        "format_version": TRAINABLE_TPEM_INTERCHANGE_VERSION,
        "d_model": dm,
        "num_ternary_blocks": nblk,
        "io_d_model": iodm,
        "tensor_layout": "safetensors_f32",
        "meta": dict(meta) if meta else {},
    }
    env_json = json.dumps(env, separators=(",", ":")).encode("utf-8")
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    out = bytearray()
    out.extend(TPEM_INTERCHANGE_MAGIC)
    out.extend(len(env_json).to_bytes(8, "little"))
    out.extend(env_json)
    out.extend(st_bytes)
    p.write_bytes(out)
    logger.info("Wrote trainable TPEM interchange v2 to %s", p)


def _load_interchange_v2_from_bytes(model: Any, raw: bytes, map_location: Any) -> Dict[str, Any]:
    if len(raw) < 16:
        raise ValueError("TPEM interchange v2: file too small")
    if raw[:8] != TPEM_INTERCHANGE_MAGIC:
        raise ValueError("TPEM interchange v2: bad magic")
    jlen = int.from_bytes(raw[8:16], "little")
    if 16 + jlen > len(raw):
        raise ValueError("TPEM interchange v2: invalid envelope length")
    env = json.loads(raw[16 : 16 + jlen].decode("utf-8"))
    ver = env.get("format_version", 0)
    if ver != TRAINABLE_TPEM_INTERCHANGE_VERSION:
        logger.warning(
            "TPEM interchange format_version=%s (expected %s); loading with best effort.",
            ver,
            TRAINABLE_TPEM_INTERCHANGE_VERSION,
        )
    st_blob = raw[16 + jlen :]
    tensors = _safetensors_bytes_to_flat(st_blob, map_location)
    te_sd: Dict[str, torch.Tensor] = {}
    qr_sd: Dict[str, torch.Tensor] = {}
    block_sds: Dict[int, Dict[str, torch.Tensor]] = {}
    stem_sd: Dict[str, torch.Tensor] = {}
    head_sd: Dict[str, torch.Tensor] = {}
    p_te = "ternary_expert."
    p_qr = "quantum_router."
    p_tb = "ternary_blocks."
    for k, v in tensors.items():
        if k.startswith(p_te):
            te_sd[k[len(p_te) :]] = v
        elif k.startswith(p_qr):
            qr_sd[k[len(p_qr) :]] = v
        elif k.startswith(p_tb):
            rest = k[len(p_tb) :]
            parts = rest.split(".", 1)
            if len(parts) == 2 and parts[0].isdigit():
                bi = int(parts[0])
                block_sds.setdefault(bi, {})[parts[1]] = v
        elif k.startswith("input_stem."):
            stem_sd[k[len("input_stem.") :]] = v
        elif k.startswith("output_head."):
            head_sd[k[len("output_head.") :]] = v
    blocks = getattr(model, "ternary_blocks", None)
    if block_sds and blocks is not None:
        for bi, sd in sorted(block_sds.items()):
            if bi < len(blocks):
                inc = blocks[bi].load_state_dict(sd, strict=False)
                if inc.missing_keys or inc.unexpected_keys:
                    logger.info(
                        "ternary_blocks[%s] load_state_dict: missing=%s unexpected=%s",
                        bi,
                        inc.missing_keys,
                        inc.unexpected_keys,
                    )
    elif te_sd:
        inc = model.ternary_expert.load_state_dict(te_sd, strict=False)
        if inc.missing_keys or inc.unexpected_keys:
            logger.info(
                "ternary_expert load_state_dict: missing=%s unexpected=%s",
                inc.missing_keys,
                inc.unexpected_keys,
            )
    if stem_sd and isinstance(getattr(model, "input_stem", None), torch.nn.Module):
        inc = model.input_stem.load_state_dict(stem_sd, strict=False)
        if inc.missing_keys or inc.unexpected_keys:
            logger.info(
                "input_stem load_state_dict: missing=%s unexpected=%s",
                inc.missing_keys,
                inc.unexpected_keys,
            )
    if head_sd and getattr(model, "output_head", None) is not None:
        inc = model.output_head.load_state_dict(head_sd, strict=False)
        if inc.missing_keys or inc.unexpected_keys:
            logger.info(
                "output_head load_state_dict: missing=%s unexpected=%s",
                inc.missing_keys,
                inc.unexpected_keys,
            )
    if qr_sd:
        inc = model.quantum_router.load_state_dict(qr_sd, strict=False)
        if inc.missing_keys or inc.unexpected_keys:
            logger.info(
                "quantum_router load_state_dict: missing=%s unexpected=%s",
                inc.missing_keys,
                inc.unexpected_keys,
            )
    meta = env.get("meta")
    return dict(meta) if isinstance(meta, dict) else {}


def peek_trainable_tpem_geometry(path: str | Path) -> Dict[str, Any]:
    """Read ``d_model``, block count, and ``io_d_model`` from a trainable TPEM file (no full init).

    Supports interchange v2 (``QMWTPEM2``) and pickle v1 payloads.
    """
    p = Path(path)
    if not p.is_file():
        return {}
    try:
        raw = p.read_bytes()
    except OSError:
        return {}
    if len(raw) >= 8 and raw[:8] == TPEM_INTERCHANGE_MAGIC:
        if len(raw) < 16:
            return {}
        jlen = int.from_bytes(raw[8:16], "little")
        if 16 + jlen > len(raw):
            return {}
        env = json.loads(raw[16 : 16 + jlen].decode("utf-8"))
        dm = int(env.get("d_model", D_MODEL))
        nblk = int(env.get("num_ternary_blocks", 1))
        iodm = int(env.get("io_d_model", dm))
        return {
            "d_model": dm,
            "num_ternary_blocks": max(1, nblk),
            "io_d_model": max(8, iodm),
            "interchange_v2": True,
        }
    try:
        payload = _torch_load_compat(p, "cpu")
    except Exception:
        return {}
    if not isinstance(payload, dict):
        return {}
    dm = int(payload.get("d_model", D_MODEL))
    nblk = int(payload.get("num_ternary_blocks", 1))
    if isinstance(payload.get("ternary_blocks"), list):
        nblk = max(nblk, len(payload["ternary_blocks"]))
    iodm = int(payload.get("io_d_model", dm))
    return {
        "d_model": max(32, dm),
        "num_ternary_blocks": max(1, nblk),
        "io_d_model": max(8, iodm),
    }


def build_trainable_tpem_payload(
    model: Any,
    meta: Optional[Dict[str, Any]] = None,
    cascade_policy: Optional[Union[nn.Module, Dict[str, Any]]] = None,
) -> Dict[str, Any]:
    """Assemble a trainable-TPEM dict for ``torch.save``."""
    dm = int(getattr(model, "d_model", D_MODEL))
    nblk = int(getattr(model, "num_ternary_blocks", 1))
    iodm = int(getattr(model, "io_d_model", dm))
    payload: Dict[str, Any] = {
        "format_version": TRAINABLE_TPEM_FORMAT_VERSION,
        "d_model": dm,
        "num_ternary_blocks": nblk,
        "io_d_model": iodm,
        "quantum_router": model.quantum_router.state_dict(),
        "meta": dict(meta) if meta else {},
    }
    if nblk == 1:
        payload["ternary_expert"] = model.ternary_blocks[0].state_dict()
    else:
        payload["ternary_blocks"] = [b.state_dict() for b in model.ternary_blocks]
    stem = getattr(model, "input_stem", None)
    if isinstance(stem, torch.nn.Linear):
        payload["input_stem"] = stem.state_dict()
    oh = getattr(model, "output_head", None)
    if oh is not None and hasattr(oh, "state_dict"):
        payload["output_head"] = oh.state_dict()
    ha = getattr(model, "hybrid_adapter", None)
    if ha is not None:
        payload["hybrid_adapter"] = ha.state_dict()
    if cascade_policy is not None:
        if isinstance(cascade_policy, dict):
            payload["cascade_policy"] = cascade_policy
        else:
            payload["cascade_policy"] = cascade_policy.state_dict()
    return payload


def save_trainable_tpem_artifact(
    path: str | Path,
    model: Any,
    meta: Optional[Dict[str, Any]] = None,
    cascade_policy: Optional[nn.Module] = None,
) -> None:
    """Persist trainable submodules to a single file (creates parent directories)."""
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    payload = build_trainable_tpem_payload(model, meta=meta, cascade_policy=cascade_policy)
    torch.save(payload, p)
    logger.info("Wrote trainable TPEM artifact to %s", p)


def _torch_load_compat(path: Path, map_location: Any) -> Any:
    sig = None
    try:
        sig = inspect.signature(torch.load)
    except (TypeError, ValueError):
        pass
    if sig is not None and "weights_only" in sig.parameters:
        try:
            return torch.load(str(path), map_location=map_location, weights_only=True)
        except Exception:
            logger.debug("weights_only load failed; retrying full pickle.", exc_info=True)
    # Trusted paths only (see pyproject bandit skip B614).
    return torch.load(str(path), map_location=map_location)


def load_trainable_tpem_into_model(
    model: Any,
    path: str | Path,
    map_location: Any = "cpu",
) -> Dict[str, Any]:
    """Load trainable submodule weights; return payload ``meta`` (may be empty).

    Supports pickle v1 (``torch.save`` dict) and native **interchange v2** (``QMWTPEM2`` files
    written by the C++ engine or :func:`save_trainable_tpem_interchange_v2`).
    """
    p = Path(path)
    if not p.is_file():
        raise FileNotFoundError(f"Trainable TPEM artifact not found: {p}")

    raw = p.read_bytes()
    if len(raw) >= 8 and raw[:8] == TPEM_INTERCHANGE_MAGIC:
        return _load_interchange_v2_from_bytes(model, raw, map_location)

    payload = _torch_load_compat(p, map_location)
    if not isinstance(payload, dict):
        raise ValueError(f"Invalid trainable TPEM payload (expected dict): {p}")

    ver = payload.get("format_version", 0)
    if ver != TRAINABLE_TPEM_FORMAT_VERSION:
        logger.warning(
            "Trainable TPEM format_version=%s (expected %s); loading with best effort.",
            ver,
            TRAINABLE_TPEM_FORMAT_VERSION,
        )

    qr = payload.get("quantum_router")
    if qr is not None:
        inc = model.quantum_router.load_state_dict(qr, strict=False)
        if inc.missing_keys or inc.unexpected_keys:
            logger.info(
                "quantum_router load_state_dict: missing=%s unexpected=%s",
                inc.missing_keys,
                inc.unexpected_keys,
            )
    tb_list = payload.get("ternary_blocks")
    if isinstance(tb_list, list) and tb_list:
        blocks = getattr(model, "ternary_blocks", None)
        if blocks is not None:
            for i, sd in enumerate(tb_list):
                if i < len(blocks) and isinstance(sd, dict):
                    inc = blocks[i].load_state_dict(sd, strict=False)
                    if inc.missing_keys or inc.unexpected_keys:
                        logger.info(
                            "ternary_blocks[%s] load_state_dict: missing=%s unexpected=%s",
                            i,
                            inc.missing_keys,
                            inc.unexpected_keys,
                        )
    else:
        te = payload.get("ternary_expert")
        if te is not None:
            inc = model.ternary_expert.load_state_dict(te, strict=False)
            if inc.missing_keys or inc.unexpected_keys:
                logger.info(
                    "ternary_expert load_state_dict: missing=%s unexpected=%s",
                    inc.missing_keys,
                    inc.unexpected_keys,
                )

    stem_sd = payload.get("input_stem")
    if isinstance(stem_sd, dict) and stem_sd:
        stem = getattr(model, "input_stem", None)
        if stem is not None and hasattr(stem, "load_state_dict"):
            inc = stem.load_state_dict(stem_sd, strict=False)
            if inc.missing_keys or inc.unexpected_keys:
                logger.info(
                    "input_stem load_state_dict: missing=%s unexpected=%s",
                    inc.missing_keys,
                    inc.unexpected_keys,
                )
    ohs = payload.get("output_head")
    if isinstance(ohs, dict) and ohs and getattr(model, "output_head", None) is not None:
        inc = model.output_head.load_state_dict(ohs, strict=False)
        if inc.missing_keys or inc.unexpected_keys:
            logger.info(
                "output_head load_state_dict: missing=%s unexpected=%s",
                inc.missing_keys,
                inc.unexpected_keys,
            )

    had = payload.get("hybrid_adapter")
    if isinstance(had, dict) and had:
        attach = getattr(model, "attach_hybrid_adapter_matching_state", None)
        if callable(attach):
            attach(had)
        mod = getattr(model, "hybrid_adapter", None)
        if mod is not None:
            inc = mod.load_state_dict(had, strict=False)
            if inc.missing_keys or inc.unexpected_keys:
                logger.info(
                    "hybrid_adapter load_state_dict: missing=%s unexpected=%s",
                    inc.missing_keys,
                    inc.unexpected_keys,
                )

    cr_mod = getattr(model, "cascade_router", None)
    cp_sd = payload.get("cascade_policy")
    if cr_mod is not None and isinstance(cp_sd, dict) and cp_sd:
        inc = cr_mod.load_state_dict(cp_sd, strict=False)
        if inc.missing_keys or inc.unexpected_keys:
            logger.info(
                "cascade_router load_state_dict: missing=%s unexpected=%s",
                inc.missing_keys,
                inc.unexpected_keys,
            )

    meta = payload.get("meta")
    return dict(meta) if isinstance(meta, dict) else {}


def load_cascade_policy_from_trainable_tpem(
    policy: nn.Module,
    path: str | Path,
    map_location: Any = "cpu",
) -> bool:
    """Load ``cascade_policy`` weights from a trainable TPEM file if present."""
    p = Path(path)
    if not p.is_file():
        return False
    payload = _torch_load_compat(p, map_location)
    if not isinstance(payload, dict):
        return False
    cp = payload.get("cascade_policy")
    if not isinstance(cp, dict) or not cp:
        return False
    inc = policy.load_state_dict(cp, strict=False)
    if inc.missing_keys or inc.unexpected_keys:
        logger.info(
            "cascade_policy load_state_dict: missing=%s unexpected=%s",
            inc.missing_keys,
            inc.unexpected_keys,
        )
    return True


# --- Legacy aliases (API + older docs) ---
build_checkpoint_payload = build_trainable_tpem_payload
save_checkpoint = save_trainable_tpem_artifact
load_checkpoint_into_model = load_trainable_tpem_into_model
load_cascade_policy_from_checkpoint = load_cascade_policy_from_trainable_tpem
