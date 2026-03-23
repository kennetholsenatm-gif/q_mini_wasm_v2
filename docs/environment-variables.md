# Environment Variables

This document provides a comprehensive reference for all environment variables used in the LLM Pract project.

## Table of Contents

- [IBM Quantum](#ibm-quantum)
- [Qiskit](#qiskit)
- [Quantum Simulation](#quantum-simulation)
- [WASM](#wasm)
- [Intel ARC/GPU](#intel-arc-gpu)
- [Cloud](#cloud)
- [Database](#database)
- [Monitoring](#monitoring)
- [Docker](#docker)
- [Development](#development)
- [Security](#security)
- [Performance](#performance)
- [Intel Quantum](#intel-quantum)
- [Other](#other)

## IBM Quantum

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `IBM_QUANTUM_API_KEY`  | string | `None` | Yes | IBM Quantum API key for accessing quantum services |
| `IBM_QUANTUM_URL`  | string | `None` | Yes | IBM Quantum API endpoint URL |

## Qiskit

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `QISKIT_LOAD_ACCOUNT`  | string | `None` | Yes | Whether to load Qiskit account credentials |
| `QISKIT_SAVE_ACCOUNT`  | string | `None` | Yes | Whether to save Qiskit account credentials |

## Quantum Simulation

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `INTEL_QUANTUM_SDK_PATH`  | string | `None` | Yes | Path to Intel Quantum SDK installation |
| `QUANTUM_SEED`  | string | `None` | Yes | Random seed for quantum simulation (optional) |
| `QUANTUM_SHOTS`  | string | `None` | Yes | Number of shots for quantum circuit execution |
| `QUANTUM_SIMULATOR`  | string | `None` | Yes | Quantum simulator backend to use (e.g., AerSimulator) |

## WASM

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `WASM_CACHE_DIR`  | string | `None` | Yes | Directory for WASM cache files |
| `WASM_MAX_MEMORY`  | string | `None` | Yes | Maximum memory allocation for WASM runtime in MB |
| `WASM_RUNTIME`  | string | `None` | Yes | WASM runtime to use (e.g., wasmtime) |

## Intel ARC/GPU

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `INTEL_GPU_DEVICE`  | string | `None` | Yes | Intel GPU device ID |
| `INTEL_GPU_MEMORY`  | string | `None` | Yes | Intel GPU memory allocation in MB |
| `INTEL_GPU_THREADS`  | string | `None` | Yes | Number of threads for Intel GPU processing |

## Cloud

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `AWS_ACCESS_KEY_ID`  | string | `None` | Yes | AWS access key ID for cloud services |
| `AWS_REGION`  | string | `None` | Yes | AWS region for cloud services |
| `AWS_SECRET_ACCESS_KEY`  | string | `None` | Yes | AWS secret access key for cloud services |
| `CLOUD_PROVIDER`  | string | `None` | Yes | Cloud provider (e.g., aws, azure, gcp) |
| `INTEL_CLOUD_API_KEY`  | string | `None` | Yes | Intel Arc cloud API key |
| `INTEL_CLOUD_ENDPOINT`  | string | `None` | Yes | Intel Arc cloud endpoint URL |
| `INTEL_CLOUD_PROJECT_ID`  | string | `None` | Yes | Intel Arc cloud project ID |

## Database

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `DATABASE_URL`  | string | `None` | Yes | PostgreSQL database connection URL |
| `REDIS_URL`  | string | `None` | Yes | Redis connection URL |

## Monitoring

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `GRAFANA_PORT`  | string | `None` | Yes | Grafana web interface port |
| `NODE_EXPORTER_PORT`  | string | `None` | Yes | Node exporter metrics port |
| `PROMETHEUS_PORT`  | string | `None` | Yes | Prometheus metrics server port |

## Docker

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `DOCKER_PATH`  | string | `None` | Yes | Path to Docker executable (leave empty to use Docker from PATH) |

## Development

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `DEBUG`  | string | `None` | Yes | Enable debug mode for development |
| `LOG_LEVEL`  | string | `None` | Yes | Logging level (DEBUG, INFO, WARNING, ERROR, CRITICAL) |
| `MAX_WORKERS`  | string | `None` | Yes | Maximum number of worker processes |
| `PORT`  | string | `None` | Yes | Application server port |
| `WORKER_TIMEOUT`  | string | `None` | Yes | Worker timeout in seconds |

## Security

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `ENCRYPTION_KEY`  | string | `None` | Yes | Encryption key for data protection |
| `JWT_SECRET`  | string | `None` | Yes | JWT secret key for token authentication |
| `SECRET_KEY`  | string | `None` | Yes | Application secret key for session management |

## Performance

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `BATCH_SIZE`  | string | `None` | Yes | Batch size for processing operations |
| `MAX_TOKENS`  | string | `None` | Yes | Maximum number of tokens for processing |
| `RETRY_ATTEMPTS`  | string | `None` | Yes | Number of retry attempts for failed operations |

## Intel Quantum

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `INTEL_QS_PYTHON`  | string | `None` | Yes | Python interpreter for Intel QS |

## Other

| Variable | Type | Default | Required | Description |
|----------|------|---------|----------|-------------|
| `PREFER_XPU`  | string | `None` | Yes | Set to 0 to force CPU even when XPU is available, 1 to prefer XPU |

## Usage Notes

### Sensitive Variables

Variables marked with 🔒 are sensitive and should never be committed to version control:
- `IBM_QUANTUM_API_KEY`
- `AWS_SECRET_ACCESS_KEY`
- `SECRET_KEY`
- `JWT_SECRET`
- `ENCRYPTION_KEY`
- `INTEL_CLOUD_API_KEY`

### Configuration Sources

Environment variables can be set through:
1. **Environment**: Directly in your shell or CI/CD pipeline
2. **.env files**: For local development (ensure .env files are in .gitignore)
3. **Secrets management**: Use tools like HashiCorp Vault, AWS Secrets Manager, or Kubernetes secrets for production

### Variable Precedence

When the same variable is defined in multiple places, the precedence (from highest to lowest) is:
1. Direct environment variable
2. .env.local
3. .env.development/.env.production
4. .env

### Development vs Production

Some variables have different requirements in development vs production:
- `DEBUG`: Should be `false` in production
- `LOG_LEVEL`: Consider `WARNING` or `ERROR` in production
- Sensitive variables: Always use secrets management in production

## Related Documentation

- [Configuration Guide](Configuration.md)
- [Development Guide](wiki/Development.md)
- [Deployment Guide](wiki/Deployment-Guide.md)
