"""WUI API routers."""

from .hardware import router as hardware_router
from .quantum import router as quantum_router
from .circuits import router as circuits_router
from .deploy import router as deploy_router
from .job_config import router as job_config_router
from .training import router as training_router

__all__ = [
    "hardware_router",
    "quantum_router",
    "circuits_router",
    "deploy_router",
    "job_config_router",
    "training_router",
]
