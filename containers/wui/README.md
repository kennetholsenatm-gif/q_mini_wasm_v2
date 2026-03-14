# Q-Mini-WASM Deployment Tool (WUI) Container

Self-contained, Iron Bank / Big Bang compatible container for the WUI (backend + frontend).

## Build

From the **repository root**:

```bash
docker build -f containers/wui/Dockerfile -t qminiwasm-wui:0.1.0 .
```

Use the root `.dockerignore` to keep build context small (only `wui/` and `containers/wui/` are needed).

## Run

```bash
docker run --rm -p 8000:8000 qminiwasm-wui:0.1.0
```

- UI: http://localhost:8000/
- API: http://localhost:8000/api/
- Health: http://localhost:8000/health

The container runs as **non-root** (uid 1000).

## Iron Bank

1. Replace the runtime base in the Dockerfile with your approved Iron Bank UBI8 base (e.g. `registry1.dso.mil/ironbank/redhat/ubi/ubi8:8.x`).
2. Use `hardening_manifest.yaml` and the Iron Bank pipeline to fetch approved Python packages and build.
3. Push the image to your Iron Bank registry.

## Big Bang

Use the Helm chart under `charts/qminiwasm-wui/`. Set `image.repository` to your Iron Bank image and deploy via Flux/Helm.
