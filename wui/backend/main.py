from fastapi import FastAPI


def create_app() -> FastAPI:
    """
    Application factory for the WUI backend.

    This service is intentionally small and focused:
    - Stores provider metadata and secret references (not secrets themselves)
    - Exposes APIs for managing providers and deployment requests
    - Delegates actual infrastructure changes to CI + OpenTofu
    """
    app = FastAPI(
        title="QMiniWASM WUI Backend",
        version="0.1.0",
        description="Backend API for managing cloud/HPC/quantum providers and OpenTofu deployments.",
    )

    @app.get("/health", tags=["system"])
    async def health() -> dict:
        """
        Lightweight health check endpoint.

        CI, GitHub Actions, and external monitors can use this to verify
        that the backend process is running.
        """
        return {"status": "ok"}

    return app


app = create_app()

