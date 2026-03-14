"""Hardware (accelerator) selection API."""

from fastapi import APIRouter

from ..models import (
    AcceleratorType,
    ExecutionMode,
    HardwareCurrent,
    HardwareCurrentUpdate,
    HardwareOption,
    HpcConnectionDetails,
)

# In-memory current selection (overridden by PUT).
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


def _hpc_connection_safe(hpc: dict | None) -> HpcConnectionDetails | None:
    """Return HPC connection for response; do not expose auth_token."""
    if not hpc:
        return None
    return HpcConnectionDetails(
        endpoint_url=hpc.get("endpoint_url"),
        auth_token=None,  # Never return token
        cluster_id=hpc.get("cluster_id"),
        node_id=hpc.get("node_id"),
    )


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
    mode = _current.get("execution_mode", ExecutionMode.hpc.value)
    if isinstance(mode, ExecutionMode):
        mode = mode.value
    mode_enum = ExecutionMode(mode)
    acc = _current.get("accelerator")
    local_acc_raw = _current.get("local_accelerator")
    if mode_enum == ExecutionMode.local and local_acc_raw is not None:
        acc = local_acc_raw.value if isinstance(local_acc_raw, AcceleratorType) else str(local_acc_raw)
    if acc is None:
        if s.prefer_cuda:
            acc = "cuda"
        elif s.prefer_xpu:
            acc = "xpu"
        else:
            acc = "cpu"
    idx = _current.get("device_index", s.device_index)
    hpc = _hpc_connection_safe(_current.get("hpc_connection"))
    local_acc = None
    if local_acc_raw is not None:
        local_acc = local_acc_raw if isinstance(local_acc_raw, AcceleratorType) else AcceleratorType(str(local_acc_raw))
    simulated_quantum = _current.get("simulated_quantum", False)
    return HardwareCurrent(
        accelerator=acc,
        device_index=idx,
        device_name=_device_name(acc, idx),
        execution_mode=mode_enum,
        hpc_connection=hpc,
        local_accelerator=local_acc,
        simulated_quantum=bool(simulated_quantum),
    )


@router.put("/current", response_model=HardwareCurrent)
async def put_hardware_current(body: HardwareCurrentUpdate) -> HardwareCurrent:
    """Set current accelerator, device index, execution mode, and optional HPC connection."""
    _current["accelerator"] = body.accelerator.value
    _current["device_index"] = body.device_index
    if body.execution_mode is not None:
        _current["execution_mode"] = body.execution_mode.value
    if body.hpc_connection is not None:
        _current["hpc_connection"] = {
            "endpoint_url": body.hpc_connection.endpoint_url,
            "auth_token": body.hpc_connection.auth_token,
            "cluster_id": body.hpc_connection.cluster_id,
            "node_id": body.hpc_connection.node_id,
        }
    if body.local_accelerator is not None:
        _current["local_accelerator"] = body.local_accelerator
    if body.simulated_quantum is not None:
        _current["simulated_quantum"] = body.simulated_quantum
    return await get_hardware_current()
