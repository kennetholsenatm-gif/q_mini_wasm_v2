# WUI Application Backend (FastAPI). Build from repo root: docker build -f wui/backend/Dockerfile .
# Base: AlmaLinux 9 per DockerOS standard (https://hub.docker.com/_/almalinux)
# Rebuild regularly so base image and dnf update pull latest security patches (Trivy/Code Scanning).
FROM almalinux:9.7-20260129

RUN dnf update -y --setopt=install_weak_deps=False \
    && dnf install -y --setopt=install_weak_deps=False python3.11 python3.11-pip \
    && dnf clean all \
    && groupadd -r app --gid 1000 \
    && useradd -r -u 1000 -g app -d /app -s /sbin/nologin -c "WUI app" app

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PIP_NO_CACHE_DIR=1

WORKDIR /app

COPY wui/backend/requirements.txt /tmp/requirements.txt
RUN python3.11 -m pip install --no-cache-dir -r /tmp/requirements.txt && rm /tmp/requirements.txt

COPY wui/ /app/wui/

RUN chown -R app:app /app

USER app
EXPOSE 8000
CMD ["python3.11", "-m", "uvicorn", "wui.backend.main:app", "--host", "0.0.0.0", "--port", "8000"]
