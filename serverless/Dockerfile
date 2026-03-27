# AWS Public ECR mirrors docker.io/library/* and avoids Docker Hub rate limits.
# Debian Bookworm + Python 3.12 (same major as prior AlmaLinux-based image).
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
