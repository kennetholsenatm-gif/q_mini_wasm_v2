# WUI Application Backend (FastAPI). Build from repo root: docker build -f wui/backend/Dockerfile .
FROM python:3.11-slim

RUN groupadd -r app --gid 1000 && useradd -r -u 1000 -g app -d /app -s /sbin/nologin -c "WUI app" app

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PIP_NO_CACHE_DIR=1

WORKDIR /app

COPY wui/backend/requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir -r /tmp/requirements.txt && rm /tmp/requirements.txt

COPY wui/ /app/wui/

RUN chown -R app:app /app

USER app
EXPOSE 8000
CMD ["python", "-m", "uvicorn", "wui.backend.main:app", "--host", "0.0.0.0", "--port", "8000"]
