# ML engine image (training entrypoint). AlmaLinux 9 matches CI lint/security containers.
# Build from repo root: docker build -f wui/backend/Dockerfile -t qminiwasm-backend:local .

FROM almalinux:9.7-20260129

WORKDIR /app

RUN dnf install -y python3.11 python3.11-pip git \
    && dnf clean all

ENV PIP_NO_CACHE_DIR=1 \
    PYTHONUNBUFFERED=1

COPY requirements.txt requirements/ setup.py pyproject.toml README.md ./
COPY qminiwasm ./qminiwasm
COPY engine ./engine

RUN python3.11 -m pip install --upgrade pip \
    && python3.11 -m pip install -r requirements.txt \
    && python3.11 -m pip install -e .

CMD ["python3.11", "-m", "engine"]
