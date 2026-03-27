# Quickstart 0 to 1

This guide is the single happy path for getting `qminiwasm-core` running locally on `localhost:8080`.

## Goal

By the end of this guide, you will:

- install dependencies
- start the local inference service
- verify the service with one health check and one inference call

## Prerequisites

- Python 3.10+
- `pip`

## Step 1: Clone and enter the repository

```bash
git clone <repository-url>
cd qminiwasm-core
```

## Step 2: Install runtime dependencies

```bash
pip install -e .
pip install -e ".[serve]"
```

## Step 3: Start the local API on port 8080

```bash
uvicorn qminiwasm.engine.serve:app --host 127.0.0.1 --port 8080
```

Keep this terminal running.

## Step 4: Verify the service is healthy

In a second terminal:

```bash
curl http://127.0.0.1:8080/health
```

Expected response:

```json
{"status":"ok"}
```

## Step 5: Run one baseline inference request

In a second terminal:

```bash
python -c "import json,urllib.request;data=json.dumps({'hidden_states': [[0.0]*4096]}).encode();req=urllib.request.Request('http://127.0.0.1:8080/infer', data=data, headers={'Content-Type':'application/json'});print(urllib.request.urlopen(req).read().decode())"
```

Expected result:

- JSON response with an `output` key containing one 4096-length vector

## Production identity

Beyond this local quickstart, hardened deployments authenticate the **host** (X.509 PKI over mTLS 1.3) and **workload identity** (OIDC/OAuth2 JWT claims for tier, footprint, and topic scopes). See [`docs/IDENTITY_STACK_REFERENCE.md`](../IDENTITY_STACK_REFERENCE.md) for the reference pattern; acronym and vocabulary: [`docs/GLOSSARY.md`](../GLOSSARY.md). End-to-end runtime flow: [`docs/architecture/JOURNEY_OF_A_VECTOR.md`](../architecture/JOURNEY_OF_A_VECTOR.md).

## What to do next

- Strategic architecture path: [../../README.md](../../README.md#how-we-stay-within-the-constraint)
- Data and training references: [../TRAINING_DATA.md](../TRAINING_DATA.md)
- Quantum mode details: [../QUANTUM_QISKIT.md](../QUANTUM_QISKIT.md)
- Operator workflows: [../operations/OPERATIONS_RUNBOOK.md](../operations/OPERATIONS_RUNBOOK.md)
