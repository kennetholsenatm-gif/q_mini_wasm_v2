import os

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .config import get_settings
from .routers import hardware_router, quantum_router, circuits_router, deploy_router


def create_app() -> FastAPI:
    """
    Application factory for the WUI backend.

    This service is intentionally small and focused:
    - Stores provider metadata and secret references (not secrets themselves)
    - Exposes APIs for hardware, quantum backend, circuits, and deployment
    - Delegates actual infrastructure changes to CI + OpenTofu
    """
    get_settings()
    app = FastAPI(
        title="QMiniWASM WUI Backend",
        version="0.1.0",
        description="Backend API for managing cloud/HPC/quantum providers and OpenTofu deployments.",
    )

    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        allow_credentials=True,
        allow_methods=["*"],
        allow_headers=["*"],
    )

    @app.get("/health", tags=["system"])
    async def health() -> dict:
        """
        Lightweight health check endpoint.

        CI, GitHub Actions, and external monitors can use this to verify
        that the backend process is running.
        """
        return {"status": "ok"}

    app.include_router(hardware_router)
    app.include_router(quantum_router)
    app.include_router(circuits_router)
    app.include_router(deploy_router)

    static_dir = os.environ.get("STATIC_DIR", "")
    if static_dir and os.path.isdir(static_dir):
        from fastapi.staticfiles import StaticFiles
        app.mount("/", StaticFiles(directory=static_dir, html=True), name="static")

    return app


app = create_app()

