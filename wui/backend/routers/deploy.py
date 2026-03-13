"""Deployment (OpenTofu desired) API."""

from fastapi import APIRouter, HTTPException

from ..models import DeployDesiredRequest, DeployDesiredResponse
from ..services.opentofu_desired import write_desired_tfvars

router = APIRouter(prefix="/api/deploy", tags=["deploy"])


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
