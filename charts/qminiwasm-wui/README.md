# qminiwasm-wui Helm Chart

Deploys the Q-Mini-WASM Deployment Tool (WUI) on Kubernetes. Big Bang compatible (non-root, securityContext, optional Ingress).

## Install

```bash
helm install qminiwasm-wui ./charts/qminiwasm-wui -n <namespace> --create-namespace
```

## Iron Bank image

Set the image to your Iron Bank registry:

```yaml
image:
  repository: registry1.dso.mil/<path>/qminiwasm-wui
  tag: "0.1.0"
  pullPolicy: Always
imagePullSecrets:
  - name: registry-credentials
```

## Values

| Key | Default | Description |
|-----|---------|-------------|
| replicaCount | 1 | Number of replicas |
| image.repository | qminiwasm-wui | Image name or full registry path |
| image.tag | 0.1.0 | Image tag |
| service.port | 8000 | Service port |
| podSecurityContext.runAsUser | 1000 | Non-root UID (Iron Bank) |
| ingress.enabled | false | Enable Ingress |
| env | {} | Extra env vars (e.g. OPENTOFU_DESIRED_DIR) |
