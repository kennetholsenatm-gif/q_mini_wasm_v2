# Q-Mini-WASM Deployment Tool (WUI) Container

Self-contained container for the WUI (backend + frontend). DockerOS standard is **AlmaLinux 9** with **Foreman** and **Foreman Smart Proxy** for host lifecycle and Capsule-like services (see [DockerOS Platform Standard](../../docs/DockerOS-Platform-Standard.md)).

## Build

From the **repository root**:

**AlmaLinux 9 (FOSS standard):**
```bash
docker build -f containers/wui/Dockerfile.almalinux -t qminiwasm-wui:0.1.0 .
```

**UBI8 (legacy / Iron Bank):**
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

## DockerOS standard (AlmaLinux, Foreman, Smart Proxy)

- **Host OS:** AlmaLinux 9 (or 10 when available) for nodes running Docker/Kubernetes.
- **Lifecycle/content:** Foreman (Satellite FOSS equivalent); Foreman Smart Proxy for Capsule-like services.
- **Container base:** `hardening_manifest.yaml` and `Dockerfile.almalinux` use AlmaLinux 9. For Iron Bank, override the base with an approved UBI/Alma image in the Dockerfile and manifest.

## Iron Bank

1. Use an approved base (e.g. `registry1.dso.mil/ironbank/redhat/ubi/ubi8:8.x` or approved AlmaLinux) in the Dockerfile and `hardening_manifest.yaml`.
2. Run the Iron Bank pipeline to fetch approved packages and build.
3. Push the image to your Iron Bank registry.

## Big Bang

Use the Helm chart under `charts/qminiwasm-wui/`. Set `image.repository` to your registry image and deploy via Flux/Helm.
