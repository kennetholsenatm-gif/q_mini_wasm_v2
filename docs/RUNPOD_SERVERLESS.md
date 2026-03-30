# RunPod Serverless and the training WUI

This repo supports two RunPod integration modes:

| Mode | Use case | How the WUI uses it |
|------|----------|---------------------|
| **Pods** | Long-lived GPU VMs, full repo sync, multi-hour training | OpenTofu under `infra/runpod`, SSH + native **`qmw-grpc-train`** on the pod |
| **Serverless** | Queue-based GPU **jobs** (handler in your worker image) | HTTP API to `https://api.runpod.ai/v2/{ENDPOINT_ID}/…` — no OpenTofu |

**Two HTTP bases:** [Management REST API](https://docs.runpod.io/api-reference/overview) at **`https://rest.runpod.io/v1`** (Bearer auth; account **`RUNPOD_API_KEY`** / **`RUNPOD_TOKEN`**) manages **templates** and **Serverless endpoints**. The WUI proxies **Infra → RunPod Serverless** for:

- **[Templates](https://docs.runpod.io/api-reference/templates/GET/templates)** — **`GET`** still uses **`https://rest.runpod.io/v1/templates`**. **Serverless template create** in the WUI uses **GraphQL `saveTemplate`** on **`https://api.runpod.io/graphql`** ([GraphQL manage templates](https://docs.runpod.io/sdks/graphql/manage-pod-templates)) with **`isServerless: true`**, **`volumeInGb: 0`**, **`containerDiskInGb`**, **`dockerArgs`**, and **`env`** as RunPod documents — **not** REST **`POST /templates`**, which returns **“Serverless templates do not support volumeInGb”** for serverless workers. Prefer **network volumes** on the **endpoint** if you need shared storage.
- **[Endpoints](https://docs.runpod.io/api-reference/endpoints/POST/endpoints)** — `GET`/`POST /endpoints`; **`templateId`** (from a Serverless template’s **`id`**) is required to create an endpoint.

**Jobs** (health, `/run`, `/status`, …) use **`https://api.runpod.ai/v2/{ENDPOINT_ID}/…`** with **`RUNPOD_TOKEN_END`** and/or the account key as documented below.

Official RunPod docs: [Serverless overview](https://docs.runpod.io/serverless/overview), [Send API requests](https://docs.runpod.io/serverless/endpoints/send-requests), [Operation reference](https://docs.runpod.io/serverless/endpoints/operation-reference), [Handler functions](https://docs.runpod.io/serverless/workers/handler-functions), [SDKs](https://docs.runpod.io/serverless/sdks), [Hugging Face on Serverless](https://docs.runpod.io/serverless/development/huggingface-models).

## Environment (credentials)

RunPod keys are **secrets** (not training config). Put them in repo **`.env`** (see [`.env.example`](../.env.example) and [environment-variables.md](environment-variables.md)):

- **`RUNPOD_TOKEN_END`** (optional): **Endpoint API key** from the RunPod console for that Serverless endpoint. When set, the WUI uses it for **`https://api.runpod.ai/v2/{ENDPOINT_ID}/…`** (health, `/run`, `/status`, cancel, Launch training). If unset, **`RUNPOD_API_KEY`** or **`RUNPOD_TOKEN`** is used for the queue API instead.
- **`RUNPOD_API_KEY`** or **`RUNPOD_TOKEN`**: Account-wide key — required for **`https://rest.runpod.io/v1`** (list/create endpoints in the WUI). The endpoint key alone is **not** used for management REST.
- **`RUNPOD_SERVERLESS_ENDPOINT_ID`** — endpoint id from the console (Serverless → your endpoint)

Restart **training-wui** after editing `.env`.

## Payload and timeouts

Per RunPod’s handler documentation, queue endpoints enforce payload limits (on the order of **10 MB** for `/run` and **20 MB** for `/runsync`). Large configs, datasets, and checkpoints must live on the worker (image, network volume, or object storage), not in the JSON body.

Jobs support `policy.executionTimeout` and `policy.ttl` (milliseconds). The WUI’s **Launch** path for `run_target: runpod_serverless` sends a default policy with **48h** execution and **72h** TTL; adjust in code if your jobs need different bounds.

## WUI → worker contract (`runpod_serverless` training)

When you **Launch training** with execution target **RunPod Serverless**, the WUI submits an async **`POST …/run`** body shaped like:

```json
{
  "input": {
    "qmw_schema_version": 1,
    "qmw_action": "train",
    "config_rel": "configs/training/your.toml",
    "extra_env": ["KEY=value", "..."]
  },
  "policy": {
    "executionTimeout": 172800000,
    "ttl": 259200000
  }
}
```

- **`config_rel`**: path relative to the **worker’s** checkout of the repo (your handler must know where the repo root is).
- **`extra_env`**: quantum-related overrides from the WUI (`buildQuantumEnvOverrides`), same idea as local runs. Edge profiling may add `QMW_DISABLE_TROPICAL_ATTN=1` for Tier 1 runs.
- **`toml_overlay`** (optional): TOML fragment (non-secret) **deep-merged** into the file at `config_rel` on the worker before **`qmw-grpc-train`** runs. Omit the field when unused. The WUI caps size at **64 KiB**. The reference handler at `serverless/handler.py` implements merge using **`tomli-w`**; install **`llm-pract[training]`** (or `tomli-w`) in the worker image when using overlays.

Your worker must implement this input (or a superset). The repository does **not** ship a production Serverless worker image; you build a container with `qminiwasm-core`, a built C++ **`qminiwasm_training_engine_server`**, Go **`qmw-grpc-train`** on `PATH` (or **`QMW_GRPC_TRAIN_BIN`**), and the RunPod handler in `serverless/handler.py` (after optional overlay merge).

## HTTP API (training-wui)

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/runpod/serverless/meta` | Endpoint id configured, token present, doc links |
| GET | `/api/runpod/serverless/endpoints` | Proxies RunPod management **`GET https://rest.runpod.io/v1/endpoints`**; optional query `includeTemplate=true`, `includeWorkers=true`; JSON `{ "ok", "endpoints": [...] }` |
| POST | `/api/runpod/serverless/endpoints` | Proxies **`POST https://rest.runpod.io/v1/endpoints`**; JSON body must include **`templateId`**; JSON `{ "ok", "endpoint": { … } }` on success ([create endpoint](https://docs.runpod.io/api-reference/endpoints/POST/endpoints)) |
| GET | `/api/runpod/serverless/templates` | Proxies **`GET https://rest.runpod.io/v1/templates`**; optional query `includeEndpointBoundTemplates=true`, `includePublicTemplates=true`, `includeRunpodTemplates=true`; JSON `{ "ok", "templates": [...] }` ([list templates](https://docs.runpod.io/api-reference/templates/GET/templates)) |
| POST | `/api/runpod/serverless/templates` | Creates a Serverless template via **GraphQL `saveTemplate`** on **`https://api.runpod.io/graphql`** (same account key as management REST). JSON body must include **`name`** and **`imageName`**; optional **`dockerArgs`** (default `python /app/serverless/handler.py`), **`containerDiskInGb`** (default 50), **`env`**, **`readme`**, **`containerRegistryAuthId`**. Returns `{ "ok", "template": { "id", … } }` — use **`id`** as **`templateId`** for endpoints. REST **`POST https://rest.runpod.io/v1/templates`** is not used for Serverless (RunPod rejects **`volumeInGb`** there). See [GraphQL manage templates](https://docs.runpod.io/sdks/graphql/manage-pod-templates). |
| GET | `/api/runpod/serverless/health` | Proxies RunPod `GET …/health` |
| GET | `/api/runpod/serverless/worker-image?config=…` | Resolves the Docker image for template creation: non-empty **`[runpod_serverless] worker_image`** in the given training TOML, else the built-in AlmaLinux 10 worker image; JSON `{ "ok", "image", "source": "toml" \| "builtin" }` |
| POST | `/api/runpod/serverless/run` | Proxies `/run` or `/runsync` (body must include `input`; optional `mode`: `"sync"`, `wait_ms`) |
| GET | `/api/runpod/serverless/job?id=JOB_ID` | Proxies `GET …/status/JOB_ID` |

`GET /api/meta` includes a **`runpod_serverless`** summary for the UI, including **`default_worker_image`** (built-in AlmaLinux 10 worker image default, same as the **`builtin`** path above). **`default_container_disk_gb`** remains in **`/api/meta`** for older UI bits but is **not** sent on Serverless template create (RunPod rejects disk/volume fields there).

**Training TOML:** Optional section **`[runpod_serverless]`** with **`worker_image`** (string) overrides that default for **Launch** / **Provision** when the Advanced “override worker image” field is empty. The training engine ignores this table; it exists for WUI and documentation.

**Training WUI:** With execution target **RunPod Serverless**, **Launch training** (step 5) calls **`GET /api/runpod/serverless/worker-image`** using the same config path as launch, then runs management **POST /templates** then **POST /endpoints** when no endpoint id is set (ephemeral `qmw-tpl-*` / `qmw-ep-*` names), then starts the job—no confirmation dialog. **Advanced Serverless overrides** in step 2 can set a reusable endpoint id or image override; **Provision template and endpoint only** runs create without starting training (with a confirmation). **Infra → RunPod Serverless** still has **Autofill from wizard** for the JSON textareas.

### Example: `POST /api/runpod/serverless/templates` body (WUI → GraphQL)

The WUI maps this JSON into **`saveTemplate`** variables. Replace **`name`** and **`imageName`**.

```json
{
  "name": "my-worker",
  "imageName": "docker.io/youruser/qmw-worker:v1",
  "dockerArgs": "python /app/serverless/handler.py",
  "containerDiskInGb": 50
}
```

Optional: **`env`** (object of string → string, converted to GraphQL env pairs), **`readme`**, **`containerRegistryAuthId`**. If omitted, the WUI supplies **`dockerArgs`** = `python /app/serverless/handler.py`, **`containerDiskInGb`** = 50, **`volumeInGb`** = 0, and **`isServerless`** = true for GraphQL.

### Build a worker image from this repo

This repo now includes:

- `serverless/handler.py` — RunPod handler that runs **`qmw-grpc-train`** against **`config_rel`** (C++ gRPC server must be running in the container)
- `serverless/Dockerfile` — AlmaLinux 10 base image + editable install + runpod worker runtime

Example:

```bash
docker build -f serverless/Dockerfile -t docker.io/<you>/qmw-serverless:latest .
docker push docker.io/<you>/qmw-serverless:latest
```

### Provision a template and endpoint with your image

1. Build/push your image.
2. In the wizard, pick execution target **RunPod Serverless**.
3. Set image either by:
   - Advanced Serverless image override, or
   - Training TOML:

```toml
[runpod_serverless]
worker_image = "docker.io/<you>/qmw-serverless:latest"
```

4. Use **Provision template and endpoint only** (or Launch with no endpoint configured) so the WUI creates template + endpoint and then submits training jobs.

## Launch training

`POST /api/runs` accepts `run_target`: `"local"` | `"runpod"` | `"runpod_serverless"`. Optional `runpod_serverless_endpoint_id` overrides `RUNPOD_SERVERLESS_ENDPOINT_ID` for that request.

The WUI polls **`/status`** until the job reaches a terminal state and appends the JSON result to the run log. **Stop** calls RunPod **`/cancel/{jobId}`** when possible.

## Troubleshooting

- Queue auth errors: set `RUNPOD_TOKEN_END` (or fallback `RUNPOD_API_KEY` / `RUNPOD_TOKEN`) and restart `training-wui`.
- `config not found` in worker result: ensure `config_rel` points to a file that exists under `/app` (or your `QMW_REPO_ROOT`).
- `Missing dependency: runpod`: ensure image installs `runpod` and starts with `python /app/serverless/handler.py`.
- Management API auth failures: template/endpoint operations require account-wide key (`RUNPOD_API_KEY` or `RUNPOD_TOKEN`).

## When to prefer Pods vs Serverless

- **Pods**: Familiar SSH workflow, full tree sync, long uninterrupted training, OpenTofu lifecycle in-repo.
- **Serverless**: Autoscaling workers, pay-per-job, custom handler — better once you have a **worker image** and accept queue/cold-start behavior.

For conceptual background, see [RunPod Serverless overview](https://docs.runpod.io/serverless/overview).
