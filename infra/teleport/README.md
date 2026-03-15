# Teleport Helm values

| File | Use case |
|------|----------|
| **teleport-values.yaml** | Production / Zero Trust: OIDC + WebAuthn, no local auth. Create the OIDC connector (tctl or Operator) before or after install. |
| **teleport-values-local.yaml** | Local/Kind: local auth so the auth service starts without an IdP. Use with `environments/local` (default in terraform.tfvars.example). Create users with `tctl users add`. **Requires Teleport chart 16.1.0** (17/18 reject password-only MFA). |

For local dev, `make -C infra/opentofu local-up` uses `teleport-values-local.yaml` and chart version 16.1.0 by default. If you use Helm directly, run: `helm upgrade teleport teleport/teleport-cluster -n teleport -f infra/teleport/teleport-values-local.yaml --version 16.1.0`.

## Troubleshooting: auth CrashLoopBackOff

**1. Get the crash reason (use the current auth pod name from `kubectl get pods -n teleport`):**
```bash
kubectl logs -n teleport deployment/teleport-auth --tail=80
# or by pod name:
kubectl logs -n teleport teleport-auth-5669655d88-rfkkt --tail=80
```

**2. If you switched from OIDC to local (or changed clusterName):** Teleport’s data volume may still be from the old config. Delete the auth PVC so it re-initializes with the new settings:
```bash
kubectl get pvc -n teleport
# Delete the auth server PVC (name often contains "auth" or "data")
kubectl delete pvc -n teleport teleport
kubectl delete pod -n teleport -l app.kubernetes.io/name=teleport-auth
# If the label doesn't match, delete the auth pod by name from get pods
```
Then check again: `kubectl
 get pods -n teleport` (auth will get a new PVC and start clean).

**3. Confirm the release is using local values:** `helm get values teleport -n teleport` and check that `authentication.type` is `local` and `localAuth` is `true`. If not, re-apply with `teleport_values_file = "../../../teleport/teleport-values-local.yaml"` and run `tofu apply` (or `make -C infra/opentofu local-up`) from `environments/local`.

## Local Kind: access via Teleport

To access the Kind cluster through Teleport (port-forward proxy, then `tsh login` and `tsh kube login qminiwasm-local`), see the [Local Kind: access via Teleport](../opentofu/environments/local/README.md#local-kind-access-via-teleport) section in the local environment README.
