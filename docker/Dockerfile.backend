# Backend image for CI Trivy/SBOM scans (build context = repo root).
# Kept minimal so `docker build` is fast; Python deps are covered by pip-audit in CI.
FROM almalinux:9.7-20260129
RUN dnf install -y python3.11 python3.11-pip && dnf clean all
WORKDIR /srv
ENV PYTHONUNBUFFERED=1
CMD ["python3.11", "-c", "print('qminiwasm-backend placeholder')"]
