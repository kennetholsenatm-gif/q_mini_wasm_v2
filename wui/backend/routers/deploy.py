"""Deployment (OpenTofu desired) API."""

from fastapi import APIRouter, HTTPException

from ..models import (
    DeployDesiredRequest,
    DeployDesiredResponse,
    DeployTfvarsResponse,
    TfvarPreset,
)
from ..services.opentofu_desired import list_desired_tfvars, write_desired_tfvars

TFVAR_PRESETS = [
    TfvarPreset(id="lambda-default", label="Lambda Labs (default)", provider_id="lambda"),
    TfvarPreset(id="runpod-gpu", label="RunPod GPU", provider_id="runpod", instance_type="GPU"),
    TfvarPreset(id="ibm-cloud", label="IBM Cloud", provider_id="ibm_cloud"),
]

router = APIRouter(prefix="/api/deploy", tags=["deploy"])


@router.get("/tfvars", response_model=DeployTfvarsResponse)
async def get_deploy_tfvars() -> DeployTfvarsResponse:
    """List existing tfvars files and preconfigured presets."""
    files = list_desired_tfvars()
    return DeployTfvarsResponse(files=files, presets=TFVAR_PRESETS)


@router.post("/desired", response_model=DeployDesiredResponse)
async def post_deploy_desired(body: DeployDesiredRequest) -> DeployDesiredResponse:
    """Write OpenTofu desired state and return config path. CI runs opentofu apply on push."""
    try:
        basename, full_path = write_desired_tfvars(
            provider_id=body.provider_id,
            instance_type=body.instance_type,
            region=body.region,
            metadata=body.metadata,
        )
    except OSError as e:
        raise HTTPException(status_code=500, detail=f"Failed to write desired config: {e}")
    return DeployDesiredResponse(
        job="opentofu",
        status="pending",
        config_path=basename,
    )
