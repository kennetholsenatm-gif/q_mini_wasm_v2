"""Apply Unified Training Matrix phase flags to the model (freeze, Tequila deadzone)."""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

import torch.nn as nn

if TYPE_CHECKING:
    from qminiwasm.model import QMiniWASM


def _set_module_params_requires_grad(module: nn.Module | None, requires: bool) -> None:
    if module is None:
        return
    for p in module.parameters():
        p.requires_grad = requires


def set_backbone_requires_grad_except_cascade_router(model: QMiniWASM, requires: bool) -> None:
    """Set ``requires_grad`` for backbone modules; ``cascade_router`` is left unchanged here."""
    _set_module_params_requires_grad(model.quantum_router, requires)
    if isinstance(model.input_stem, nn.Linear):
        _set_module_params_requires_grad(model.input_stem, requires)
    for blk in model.ternary_blocks:
        _set_module_params_requires_grad(blk, requires)
    if model.output_head is not None:
        _set_module_params_requires_grad(model.output_head, requires)
    if model.lota_branch is not None:
        _set_module_params_requires_grad(model.lota_branch, requires)
    if model.hybrid_adapter is not None:
        _set_module_params_requires_grad(model.hybrid_adapter, requires)
    if model.tropical_attention is not None:
        _set_module_params_requires_grad(model.tropical_attention, requires)
    if model.bloch_sphere_attention is not None:
        _set_module_params_requires_grad(model.bloch_sphere_attention, requires)


def apply_phase_to_model(
    model: QMiniWASM,
    phase: dict[str, Any],
    *,
    base_tequila_deadzone: float,
) -> None:
    """Mutate model for the active phase (freeze backbone, optional Tequila schedule)."""
    router_only = bool(phase.get("router_only", False))
    freeze_bb = bool(phase.get("freeze_model_backbone", False)) or router_only

    if freeze_bb:
        set_backbone_requires_grad_except_cascade_router(model, False)
    else:
        set_backbone_requires_grad_except_cascade_router(model, True)

    if router_only and model.cascade_router is not None:
        for p in model.cascade_router.parameters():
            p.requires_grad = True

    if bool(phase.get("freeze_ternary_experts", False)) and not freeze_bb:
        freeze_ternary_weights_only(model, True)
    elif not freeze_bb:
        freeze_ternary_weights_only(model, False)

    td = phase.get("tequila_deadzone")
    if td is not None:
        v = float(td)
        for blk in model.ternary_blocks:
            blk.tequila_deadzone = v
    else:
        v0 = float(base_tequila_deadzone)
        for blk in model.ternary_blocks:
            blk.tequila_deadzone = v0


def freeze_ternary_weights_only(model: QMiniWASM, freeze: bool) -> None:
    """Freeze only ``TernaryWASMExpert.weight`` tensors (biases may still train)."""
    for blk in model.ternary_blocks:
        blk.weight.requires_grad = not freeze
