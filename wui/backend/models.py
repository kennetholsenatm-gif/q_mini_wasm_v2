from enum import Enum
from typing import Any, Dict, List, Optional

from pydantic import BaseModel, Field


# ----- Hardware -----

class AcceleratorType(str, Enum):
    cuda = "cuda"
    xpu = "xpu"
    cpu = "cpu"


class HardwareOption(BaseModel):
    id: str = Field(description="Accelerator id: cuda, xpu, cpu.")
    name: str = Field(description="Human-readable name.")


class HardwareCurrent(BaseModel):
    accelerator: str = Field(description="Current accelerator: cuda, xpu, cpu.")
    device_index: int = Field(default=0, description="Device index.")
    device_name: str = Field(description="Human-readable device name.")


class HardwareCurrentUpdate(BaseModel):
    accelerator: AcceleratorType
    device_index: int = 0


# ----- Quantum backend -----

class QuantumBackendInfo(BaseModel):
    id: str = Field(description="Backend id.")
    name: str = Field(description="Human-readable name.")
    required_env_vars: List[str] = Field(default_factory=list, description="Env var names for credentials (no values).")


class QuantumConfigResponse(BaseModel):
    backend: str = Field(description="Current backend id.")
    simulator_name: Optional[str] = None


class QuantumConfigUpdate(BaseModel):
    backend: str = Field(description="Backend id: ibm_quantum, intel_qs, penny_lane.")
    credentials: Dict[str, str] = Field(default_factory=dict, description="API keys / tokens; never logged.")


class QuantumVerifyResponse(BaseModel):
    ok: bool
    message: Optional[str] = None


# ----- Circuits (PQC) -----

class CircuitPreset(BaseModel):
    num_qubits: int
    qaoa_layers: int
    diff_method: str = "parameter-shift"


class CircuitCurrentResponse(BaseModel):
    num_qubits: int
    qaoa_layers: int
    diff_method: str


class CircuitCurrentUpdate(BaseModel):
    num_qubits: int = Field(ge=1, le=32, description="Number of qubits.")
    qaoa_layers: int = Field(ge=1, le=20, description="QAOA layers.")
    diff_method: str = Field(default="parameter-shift", description="parameter-shift or finite-diff.")


# ----- Deployment (OpenTofu) -----

class DeployDesiredRequest(BaseModel):
    provider_id: str = Field(description="Provider identifier (e.g. lambda, runpod).")
    instance_type: Optional[str] = None
    region: Optional[str] = None
    metadata: Dict[str, Any] = Field(default_factory=dict)


class DeployDesiredResponse(BaseModel):
    job: str = "opentofu"
    status: str = "pending"
    config_path: Optional[str] = None


# ----- Providers (existing) -----

class ProviderType(str, Enum):
    lambda_cloud = "lambda"
    ibm_quantum = "ibm_quantum"
    runpod = "runpod"
    ibm_cloud = "ibm_cloud"


class ProviderSecretKeys(BaseModel):
    """
    Logical keys for secrets stored in Vault.

    The actual values never leave Vault; the backend and CI only
    exchange these logical keys or Vault paths.
    """

    api_key: Optional[str] = Field(
        default=None,
        description="Logical name or Vault key for the provider API key.",
    )
    extra: Dict[str, str] = Field(
        default_factory=dict,
        description="Additional logical secret keys for this provider.",
    )


class ProviderConfig(BaseModel):
    """
    Non-secret configuration for a provider.
    """

    id: str = Field(description="Stable identifier for the provider.")
    name: str = Field(description="Human-readable provider name.")
    type: ProviderType
    region: Optional[str] = None
    metadata: Dict[str, Any] = Field(
        default_factory=dict,
        description="Arbitrary non-secret configuration (e.g. partition, instance type presets).",
    )
    secrets: ProviderSecretKeys = Field(
        default_factory=ProviderSecretKeys,
        description="Logical secret keys associated with this provider.",
    )


class ProviderCreateRequest(BaseModel):
    """
    Request payload when creating/updating a provider from the WUI.

    - `config` holds non-secret metadata.
    - `secrets` holds raw secret values that should be written to Vault.
    """

    id: str = Field(description="Stable identifier for the provider.")
    name: str
    type: ProviderType
    region: Optional[str] = None
    metadata: Dict[str, Any] = Field(default_factory=dict)
    secrets: Dict[str, str] = Field(
        default_factory=dict,
        description="Raw secret values (API keys, tokens). Must be written to Vault and never logged.",
    )


class ProviderResponse(BaseModel):
    """
    Safe representation of a provider for API responses.
    """

    id: str
    name: str
    type: ProviderType
    region: Optional[str]
    metadata: Dict[str, Any]
    # Do not expose actual secrets or Vault paths by default.

