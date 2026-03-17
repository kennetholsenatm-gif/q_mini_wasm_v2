"""WUI backend configuration via environment and .env.

Environment variables are validated at first load. When Data Stack connection
is partially configured (e.g. POSTGRES_HOST set), required companion vars must
be set or startup will fail with a clear message. See docs/Greenfield-Deployment.md
and containers/wui/README.md for WUI ↔ Data Stack connection.
"""

from functools import lru_cache
from typing import Optional

from pydantic import model_validator
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

    # Data Stack connection (optional; used when WUI talks to PostgreSQL / RabbitMQ)
    postgres_host: Optional[str] = None
    postgres_port: int = 5432
    postgres_user: Optional[str] = None
    postgres_password: Optional[str] = None
    postgres_db: str = "qminiwasm_db"
    database_url: Optional[str] = None  # alternative: postgresql://user:pass@host:port/db
    rabbitmq_host: Optional[str] = None
    rabbitmq_port: int = 5672
    rabbitmq_default_user: Optional[str] = None
    rabbitmq_default_pass: Optional[str] = None
    broker_url: Optional[str] = None  # alternative: amqp://user:pass@host:port/

    # SYCL backend (qminiwasm): sycl | stubs
    sycl_backend: str = "stubs"

    @model_validator(mode="after")
    def validate_data_stack_config(self) -> "Settings":
        """Fail fast when Data Stack vars are partially set."""
        if self.database_url:
            return self
        if self.postgres_host:
            if not self.postgres_user or not self.postgres_password:
                raise ValueError(
                    (
                        "POSTGRES_HOST is set; set POSTGRES_USER and POSTGRES_PASSWORD "
                        "(or use DATABASE_URL). See containers/wui/README.md."
                    )
                )
        if self.rabbitmq_host and not self.broker_url:
            if not self.rabbitmq_default_user or not self.rabbitmq_default_pass:
                raise ValueError(
                    (
                        "RABBITMQ_HOST is set; set RABBITMQ_DEFAULT_USER and "
                        "RABBITMQ_DEFAULT_PASS (or use BROKER_URL). See containers/wui/README.md."
                    )
                )
        return self


@lru_cache
def get_settings() -> Settings:
    return Settings()
