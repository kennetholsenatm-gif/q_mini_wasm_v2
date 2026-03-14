# Falco runtime security

Falco provides runtime detection (process, filesystem, network) for Q-Mini-WASM clusters. Events are forwarded to ELK/Splunk via Falcosidekick for NIST AU/SI continuous monitoring.

## Deploy

```bash
helm repo add falcosecurity https://falcosecurity.github.io/charts
helm repo update
helm install falco falcosecurity/falco -n falco --create-namespace -f infra/falco/falco-values.yaml
```

Configure ELK/Splunk by overriding Falcosidekick settings (do not commit secrets):

- **Elasticsearch:** Set `falcosidekick.config.elasticsearch.hostport` or env `FALCOSIDEKICK_ELASTICSEARCH_HOST`.
- **Splunk HEC:** Set `falcosidekick.config.splunk.enable: true`, `hostport`, and `hecToken` from a Kubernetes secret.

Falco emits structured JSON; Falcosidekick forwards to your endpoint. See [Alerts Forwarding](https://falco.org/docs/concepts/outputs/forwarding/).

## Custom rules

Custom rules are in `falco-values.yaml` (inline) and fully in `falco-custom-rules.yaml`. They detect:

- Shell/terminal execution in containers
- Writes to `/etc`, `/bin`, `/usr/bin`, `/sbin`
- Privilege escalation attempts (sudo, su, setuid)
- Package managers in containers (optional NOTICE)

Add or tune rules as needed; keep output in JSON-friendly form for SIEM.
