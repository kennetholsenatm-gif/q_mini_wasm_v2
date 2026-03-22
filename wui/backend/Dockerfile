# ML engine image (training entrypoint). AlmaLinux 9 matches CI lint/security containers.
# Build from repo root: docker build -f wui/backend/Dockerfile -t qminiwasm-backend:local .
#
# Uses requirements/docker.txt (no dpctl): generic Linux has no Intel oneAPI SYCL stack.

FROM almalinux:9.7-20260129

WORKDIR /app

RUN dnf install -y \
    python3.11 python3.11-pip python3.11-devel \
    git \
    gcc gcc-c++ make \
    openssl-devel libffi-devel \
    && dnf clean all

ENV PIP_NO_CACHE_DIR=1 \
    PYTHONUNBUFFERED=1

COPY requirements/docker.txt requirements/ setup.py pyproject.toml README.md ./
COPY qminiwasm ./qminiwasm
COPY engine ./engine

# CPU torch first (CI-friendly); then deps aligned with setup.py minus dpctl (see requirements/docker.txt).
RUN python3.11 -m pip install --upgrade pip setuptools wheel \
    && python3.11 -m pip install torch --index-url https://download.pytorch.org/whl/cpu \
    && python3.11 -m pip install -r requirements/docker.txt \
    && python3.11 -m pip install -e . --no-deps

CMD ["python3.11", "-m", "engine"]
