# Environment variables (secrets and APIs)

**Policy:** Variables documented here are **credentials and API keys** loaded from `.env` (or the process environment). They are **not** how you configure training, inference, accelerators, WASM limits, or feature toggles — use **`configs/training/*.toml`**, **`configs/serve/*.toml`**, **`configs/wui.toml`** (Training WUI process settings), and the **[Training WUI](../training-wui/README.md)** for that. See **[CONFIGURATION_POLICY.md](CONFIGURATION_POLICY.md)**.

For **CI, Docker, serverless, and toolchain** variables that may still be read by the codebase, see **[ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md)**.

## Table of Contents

- [Hugging Face](#hugging-face)
- [IBM Quantum](#ibm-quantum)
- [RunPod](#runpod)

## Hugging Face

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `HF_TOKEN` 🔒 | string | `None` | No | Alias for Hugging Face Hub token (some tools expect HF_TOKEN) |
| `HUGGING_FACE_HUB_TOKEN` 🔒 | string | `None` | No | API token for Hugging Face Hub (gated datasets, higher rate limits) |

## IBM Quantum

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `IBM_QUANTUM_API_TOKEN` 🔒 | string | `None` | No | IBM Quantum Platform API token (Qiskit Runtime) |
| `QISKIT_IBM_TOKEN` 🔒 | string | `None` | No | Qiskit IBM Runtime token (alternate name) |

## RunPod

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `RUNPOD_API_KEY` 🔒 | string | `None` | No | RunPod API key (alias for account key) |
| `RUNPOD_SERVERLESS_ENDPOINT_ID` | string | `None` | No | RunPod serverless endpoint ID |
| `RUNPOD_TOKEN` 🔒 | string | `None` | No | RunPod account API key (OpenTofu / management) |
| `RUNPOD_TOKEN_END` 🔒 | string | `None` | No | RunPod serverless endpoint API key (queue /run) |

## Usage notes

- Treat variables marked with 🔒 as **secrets**: never commit real values; use a secrets manager in production where applicable.
- Copy **[.env.example](../.env.example)** to `.env` for local development (`.env` is gitignored).
- **Do not** add training hyperparameters or runtime toggles to `.env` — add them to TOML or use the WUI.
- The **Training WUI** loads `.env` with an **allowlist** (secrets + `RUNPOD_SERVERLESS_ENDPOINT_ID`); other keys are ignored so configuration stays in **`configs/wui.toml`** / **`configs/runtime.toml`**.

## Related documentation

- [README](../README.md) — project overview
- [Training data](TRAINING_DATA.md)
- [ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md) — CI / container / legacy process env (not WUI user config)
