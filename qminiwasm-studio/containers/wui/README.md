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

## WUI backend → Data Stack connection

When the WUI backend (this container or the standalone backend image) must connect to the **data stack** (PostgreSQL, RabbitMQ) running in Docker or on another host, set these environment variables:

| Variable | Description | Example |
|----------|-------------|---------|
| `POSTGRES_HOST` | Data stack PostgreSQL host | `host.docker.internal`, or hostname of the data-stack host |
| `POSTGRES_PORT` | PostgreSQL port | `5432` |
| `POSTGRES_USER` | PostgreSQL user | `qminiwasm` (match data-stack `.env`) |
| `POSTGRES_PASSWORD` | PostgreSQL password | (from data-stack `.env`) |
| `POSTGRES_DB` | Database name | `qminiwasm_db` |
| `DATABASE_URL` | Alternative: full URL | `postgresql://user:password@host:5432/qminiwasm_db` |
| `RABBITMQ_HOST` | RabbitMQ host | `host.docker.internal` or data-stack host |
| `RABBITMQ_PORT` | AMQP port | `5672` |
| `RABBITMQ_DEFAULT_USER` | RabbitMQ user | `qminiwasm` |
| `RABBITMQ_DEFAULT_PASS` | RabbitMQ password | (from data-stack `.env`) |
| `BROKER_URL` | Alternative: full URL | `amqp://user:password@host:5672/` |

Example with Docker and data stack on same host:

```bash
docker run -d -p 8000:8000 \
  -e POSTGRES_HOST=host.docker.internal -e POSTGRES_PORT=5432 \
  -e POSTGRES_USER=qminiwasm -e POSTGRES_PASSWORD=your_postgres_password \
  -e POSTGRES_DB=qminiwasm_db \
  -e RABBITMQ_HOST=host.docker.internal -e RABBITMQ_PORT=5672 \
  -e RABBITMQ_DEFAULT_USER=qminiwasm -e RABBITMQ_DEFAULT_PASS=your_rabbitmq_password \
  --name wui-backend qminiwasm-wui:0.1.0
```

See [docs/Greenfield-Deployment.md](../../docs/Greenfield-Deployment.md) Step 4 for the full deployment order.

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
