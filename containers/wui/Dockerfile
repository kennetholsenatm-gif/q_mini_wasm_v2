# Q-Mini-WASM Deployment Tool (WUI) — self-contained, Iron Bank / Big Bang compatible.
# Multi-stage: build frontend, then minimal runtime with non-root user.
# For Iron Bank: replace the runtime base with the approved Iron Bank UBI8 Python image.

# -----------------------------------------------------------------------------
# Stage 1: Build frontend SPA
# -----------------------------------------------------------------------------
FROM node:24-alpine AS frontend-builder

WORKDIR /build

COPY wui/frontend/package.json wui/frontend/package-lock.json* ./
RUN npm ci 2>/dev/null || npm install
COPY wui/frontend/ .
RUN npm run build

# -----------------------------------------------------------------------------
# Stage 2: Runtime — UBI8 + Python (Iron Bank compatible)
# For Iron Bank: replace base with registry1.dso.mil/ironbank/redhat/ubi/ubi8:8.x
# and install Python per hardening_manifest.yaml.
# -----------------------------------------------------------------------------
FROM registry.access.redhat.com/ubi8/ubi:8.10 AS runtime

RUN groupadd -r app --gid 1000 && \
    useradd -r -u 1000 -g app -d /app -s /sbin/nologin -c "WUI app" app

RUN dnf install -y --nodocs python3.11 python3.11-pip && dnf clean all

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PIP_NO_CACHE_DIR=1 \
    STATIC_DIR=/app/static

WORKDIR /app

COPY containers/wui/requirements-wui.txt /tmp/requirements.txt
RUN python3.11 -m pip install --no-cache-dir -r /tmp/requirements.txt && rm /tmp/requirements.txt

COPY wui/ /app/wui/

# Static SPA from frontend build
COPY --from=frontend-builder /build/dist /app/static

# Entrypoint script
COPY containers/wui/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh && chown -R app:app /app

USER app
EXPOSE 8000
ENTRYPOINT ["/entrypoint.sh"]
