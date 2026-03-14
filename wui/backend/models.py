from enum import Enum
from typing import Any, Dict, List, Optional

from pydantic import BaseModel, Field


# ----- Hardware -----

class ExecutionMode(str, Enum):
    local = "local"
    hpc = "hpc"


class AcceleratorType(str, Enum):
    cuda = "cuda"
    xpu = "xpu"
    cpu = "cpu"


class HardwareOption(BaseModel):
    id: str = Field(description="Accelerator id: cuda, xpu, cpu.")
    name: str = Field(description="Human-readable name.")


class HpcConnectionDetails(BaseModel):
    endpoint_url: Optional[str] = Field(default=None, description="HPC cluster endpoint URL.")
    auth_token: Optional[str] = Field(default=None, description="Authentication token; never logged.")
    cluster_id: Optional[str] = Field(default=None, description="Cluster identifier.")
    node_id: Optional[str] = Field(default=None, description="Node identifier.")


class HardwareCurrent(BaseModel):
    accelerator: str = Field(description="Current accelerator: cuda, xpu, cpu.")
    device_index: int = Field(default=0, description="Device index.")
    device_name: str = Field(description="Human-readable device name.")
    execution_mode: ExecutionMode = Field(default=ExecutionMode.hpc, description="Local or HPC execution.")
    hpc_connection: Optional[HpcConnectionDetails] = Field(default=None, description="HPC connection details when mode is hpc.")
    local_accelerator: Optional[AcceleratorType] = Field(default=None, description="Local accelerator when mode is local.")
    simulated_quantum: bool = Field(default=False, description="Use simulated quantum when mode is local.")


class HardwareCurrentUpdate(BaseModel):
    accelerator: AcceleratorType
    device_index: int = 0
    execution_mode: Optional[ExecutionMode] = None
    hpc_connection: Optional[HpcConnectionDetails] = None
    local_accelerator: Optional[AcceleratorType] = None
    simulated_quantum: Optional[bool] = None


# ----- Quantum backend -----

class QuantumBackendInfo(BaseModel):
    id: str = Field(description="Backend id.")
    name: str = Field(description="Human-readable name.")
    required_env_vars: List[str] = Field(default_factory=list, description="Env var names for credentials (no values).")


class QuantumConfigResponse(BaseModel):
    backend: str = Field(description="Current backend id.")
    simulator_name: Optional[str] = None
    credentials_configured: Dict[str, bool] = Field(
        default_factory=dict,
        description="Per-env-var flag that credential is set (no values returned).",
    )
    endpoint_url: Optional[str] = Field(default=None, description="Optional endpoint URL for quantum provider.")
    provider: Optional[str] = Field(default=None, description="Provider name: ibm, ionq, intel.")


class QuantumConfigUpdate(BaseModel):
    backend: str = Field(description="Backend id: ibm_quantum, intel_qs, penny_lane, ionq.")
    credentials: Dict[str, str] = Field(default_factory=dict, description="API keys / tokens; never logged.")
    endpoint_url: Optional[str] = None
    provider: Optional[str] = None


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


class TfvarPreset(BaseModel):
    id: str = Field(description="Preset id.")
    label: str = Field(description="Display label.")
    provider_id: str = Field(description="Provider identifier.")
    instance_type: Optional[str] = None
    region: Optional[str] = None


class DeployTfvarsResponse(BaseModel):
    files: List[str] = Field(default_factory=list, description="Existing tfvars filenames.")
    presets: List[TfvarPreset] = Field(default_factory=list, description="Preconfigured presets.")


# ----- Training config -----

class TrainingConfigResponse(BaseModel):
    num_trainable_parameters: int = Field(ge=1, default=1024, description="Number of parameters to train.")
    epochs: int = Field(ge=1, default=10, description="Training epochs.")
    batch_size: int = Field(ge=1, default=32, description="Training batch size.")


class TrainingConfigUpdate(BaseModel):
    num_trainable_parameters: Optional[int] = Field(default=None, ge=1)
    epochs: Optional[int] = Field(default=None, ge=1)
    batch_size: Optional[int] = Field(default=None, ge=1)


# ----- HPC-to-Quantum transfer -----

class SerializationFormat(str, Enum):
    json = "json"
    msgpack = "msgpack"


class TransferConfig(BaseModel):
    transfer_batch_size: int = Field(ge=1, default=64, description="Batch size for HPC-to-QPU transfer.")
    serialization_format: str = Field(default="json", description="json or msgpack.")
    polling_interval_seconds: float = Field(ge=0.1, default=2.0, description="Polling interval in seconds.")


class TransferConfigUpdate(BaseModel):
    transfer_batch_size: Optional[int] = Field(default=None, ge=1)
    serialization_format: Optional[str] = Field(default=None)
    polling_interval_seconds: Optional[float] = Field(default=None, ge=0.1)


# ----- Job config (aggregate for training job) -----

class JobConfigResponse(BaseModel):
    accelerator: str = Field(description="Accelerator: cuda, xpu, cpu.")
    device_index: int = Field(default=0)
    execution_mode: str = Field(default="hpc", description="local or hpc.")
    quantum_backend: str = Field(description="Quantum backend id.")
    num_qubits: int = Field(description="QAOA num_qubits.")
    qaoa_layers: int = Field(description="QAOA layers.")
    diff_method: str = Field(description="parameter-shift or finite-diff.")
    num_trainable_parameters: int = Field(default=1024, description="Number of parameters to train.")
    epochs: int = Field(default=10, description="Training epochs.")
    batch_size: int = Field(default=32, description="Training batch size.")
    transfer_config: Optional[TransferConfig] = Field(default=None, description="HPC-to-QPU data transfer config.")


class TrainingEstimateResponse(BaseModel):
    estimated_seconds: float = Field(description="Estimated training duration in seconds.")
    message: Optional[str] = Field(default=None, description="Human-readable estimate (e.g. ~5 min).")


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

