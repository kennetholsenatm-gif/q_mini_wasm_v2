"""Hardware (accelerator) selection API."""

from fastapi import APIRouter, HTTPException

from ..models import (
    AcceleratorType,
    HardwareCurrent,
    HardwareCurrentUpdate,
    HardwareOption,
)

# In-memory current selection (overridden by PUT). Keys: accelerator, device_index.
_current: dict = {}

HARDWARE_OPTIONS = [
    HardwareOption(id="cuda", name="NVIDIA CUDA"),
    HardwareOption(id="xpu", name="Intel ARC (XPU)"),
    HardwareOption(id="cpu", name="CPU"),
]


def _device_name(accelerator: str, device_index: int) -> str:
    if accelerator == "cuda":
        return f"NVIDIA CUDA (cuda:{device_index})"
    if accelerator == "xpu":
        return f"Intel ARC (XPU:{device_index})"
    return "CPU"


router = APIRouter(prefix="/api/hardware", tags=["hardware"])


@router.get("/options", response_model=list[HardwareOption])
async def get_hardware_options() -> list[HardwareOption]:
    """Return list of supported accelerator types."""
    return HARDWARE_OPTIONS


@router.get("/current", response_model=HardwareCurrent)
async def get_hardware_current() -> HardwareCurrent:
    """Return current device selection (from stored config or env defaults)."""
    from ..config import get_settings
    s = get_settings()
    acc = _current.get("accelerator")
    if acc is None:
        if s.prefer_cuda:
            acc = "cuda"
        elif s.prefer_xpu:
            acc = "xpu"
        else:
            acc = "cpu"
    idx = _current.get("device_index", s.device_index)
    return HardwareCurrent(
        accelerator=acc,
        device_index=idx,
        device_name=_device_name(acc, idx),
    )


@router.put("/current", response_model=HardwareCurrent)
async def put_hardware_current(body: HardwareCurrentUpdate) -> HardwareCurrent:
    """Set current accelerator and device index."""
    _current["accelerator"] = body.accelerator.value
    _current["device_index"] = body.device_index
    return HardwareCurrent(
        accelerator=body.accelerator.value,
        device_index=body.device_index,
        device_name=_device_name(body.accelerator.value, body.device_index),
    )
