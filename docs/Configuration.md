# Configuration Overview

Centralized configuration and environment handling across the stack.

## WUI backend

- **Source:** [wui/backend/config.py](../wui/backend/config.py) (pydantic-settings). Loaded from environment and optional `.env` at first use.
- **Startup validation:** If `POSTGRES_HOST` is set, `POSTGRES_USER` and `POSTGRES_PASSWORD` (or `DATABASE_URL`) must be set; if `RABBITMQ_HOST` is set, `RABBITMQ_DEFAULT_USER` and `RABBITMQ_DEFAULT_PASS` (or `BROKER_URL`) must be set. The app fails fast with a clear error message when partially configured.
- **Data Stack connection:** See [containers/wui/README.md](../containers/wui/README.md) and [Greenfield-Deployment.md](Greenfield-Deployment.md) Step 4 for `POSTGRES_*`, `RABBITMQ_*`, `DATABASE_URL`, `BROKER_URL`.
- **SYCL backend:** `SYCL_BACKEND` (or `sycl_backend` in settings): `sycl` to prefer native SYCL extension, `stubs` (default) for Python stubs. See [SYCL-Integration.md](SYCL-Integration.md).

## Hierarchical inference (qminiwasm)

- **Source:** [qminiwasm/config.py](../qminiwasm/config.py). `HierarchicalConfig.from_env()` reads `N_MAX_LOOPS`, `T_CONF`, `DELTA_FORMAT_VERSION`, `TOP_K_EXPERTS`, `EXPERT_CAPACITY`.
- **Defaults:** N_max_loops=10, T_conf=0.85, delta_format_version=1.

## Security stack

- **Source:** `containers/security-stack/.env` (from `.env.example`). Keycloak and Vault credentials; see [containers/security-stack/README.md](../containers/security-stack/README.md) and [Vault-Production.md](Vault-Production.md) for production.

## Data stack

- **Source:** `containers/data-stack/.env` (from `.env.example`). PostgreSQL, RabbitMQ, NiFi, Solace; see [containers/data-stack/README.md](../containers/data-stack/README.md).
