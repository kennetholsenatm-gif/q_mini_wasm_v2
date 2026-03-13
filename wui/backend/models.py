from enum import Enum
from typing import Any, Dict, Optional

from pydantic import BaseModel, Field


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

