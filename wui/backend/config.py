"""WUI backend configuration via environment and .env."""

from functools import lru_cache
from typing import Optional

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Application settings loaded from env and .env."""

    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )

    # Hardware defaults (can be overridden by API)
    prefer_xpu: bool = True
    prefer_cuda: bool = False
    device_index: int = 0

    # Quantum backend (id)
    quantum_backend: str = "penny_lane"

    # Circuit defaults
    num_qubits: int = 8
    qaoa_layers: int = 3
    diff_method: str = "parameter-shift"

    # API / server
    debug: bool = False
    port: int = 8000

    # Optional: paths
    opentofu_desired_dir: Optional[str] = None  # default: infra/opentofu/desired relative to repo


@lru_cache
def get_settings() -> Settings:
    return Settings()
