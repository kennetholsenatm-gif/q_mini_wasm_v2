# AWS Public ECR mirrors docker.io/library/* and avoids Docker Hub rate limits.
# Debian Bookworm + Python 3.12 (same major as prior AlmaLinux-based image).
#
# Training jobs call qmw-grpc-train (see serverless/handler.py). Production images must install
# the Go binary on PATH (or set QMW_GRPC_TRAIN_BIN), build qminiwasm_training_engine_server, and
# start it before the handler runs — this slim file only installs Python deps for merge/validation.
FROM public.ecr.aws/docker/library/python:3.12-slim-bookworm

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    QMW_REPO_ROOT=/app

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    g++ \
    make \
    git \
    findutils \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY . /app

RUN python -m pip install --upgrade pip setuptools wheel && \
    python -m pip install -r requirements/docker.txt && \
    python -m pip install -e ".[training]" && \
    python -m pip install runpod && \
    python -m pip install --upgrade "cryptography>=46.0.5"

CMD ["python", "/app/serverless/handler.py"]
