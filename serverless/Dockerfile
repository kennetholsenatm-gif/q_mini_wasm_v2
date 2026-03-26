FROM almalinux:10

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    QMW_REPO_ROOT=/app

WORKDIR /app

RUN dnf -y update && \
    dnf -y install \
      python3.12 \
      python3.12-pip \
      python3.12-devel \
      gcc \
      gcc-c++ \
      make \
      git \
      which \
      findutils && \
    dnf clean all

RUN ln -sf /usr/bin/python3.12 /usr/bin/python && \
    ln -sf /usr/bin/pip3.12 /usr/bin/pip

COPY . /app

RUN python -m pip install --upgrade pip setuptools wheel && \
    python -m pip install -r requirements/docker.txt && \
    python -m pip install -e ".[training]" && \
    python -m pip install runpod && \
    python -m pip install --upgrade "cryptography>=46.0.5"

CMD ["python", "/app/serverless/handler.py"]
