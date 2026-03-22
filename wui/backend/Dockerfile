# ML engine image (training entrypoint).
# Build from repo root: docker build -f wui/backend/Dockerfile -t qminiwasm-backend:local .
#
# Debian bookworm slim: PyPI manylinux wheels (torch CPU, scipy, wasmtime, pennylane) are
# reliable here. AlmaLinux dnf stacks often hit missing wheels or long source builds in CI.
# Uses requirements/docker.txt (no dpctl). See requirements/docker.txt.

FROM python:3.11-slim-bookworm

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libffi-dev \
    libssl-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

ENV PIP_NO_CACHE_DIR=1 \
    PYTHONUNBUFFERED=1

COPY requirements/docker.txt requirements/ setup.py pyproject.toml README.md ./
COPY qminiwasm ./qminiwasm
COPY engine ./engine

# Split installs so CI logs show which step failed; CPU torch before Pennylane stack.
RUN python3 -m pip install --upgrade pip setuptools wheel
RUN python3 -m pip install torch --index-url https://download.pytorch.org/whl/cpu
RUN python3 -m pip install -r requirements/docker.txt
RUN python3 -m pip install -e . --no-deps

CMD ["python3", "-m", "engine"]
