# Identity stack reference (vendor-neutral)

This document describes a **reference** deployment pattern for **Zero-Trust Ephemeral Enrollment (ZTEE)**-aligned identity: an **internal X.509 Certificate Authority (CA)** for hardware and workload mTLS, paired with an **OIDC-compliant identity provider (IdP)** for cognitive (JWT) identity. Paths below are **examples**; your checkout may omit optional `containers/` or `infra/` trees.

## Roles

| Layer | Role | Typical technology class |
|-------|------|---------------------------|
| Internal CA | Issue and revoke short-lived mTLS certificates | LDAP-backed enterprise CA, or any CA with an IPA-style JSON control plane |
| OIDC IdP | OAuth2/OIDC tokens scoped to enclave workloads | Any standards-compliant IdP (self-hosted or cloud) |
| Secrets / audit | Key material and break-glass procedures | Vault-class secret store, HSM, or operator runbooks |

## PKI automation (this repository)

- **Canonical script:** [`scripts/security/internal_ca_pki_provisioning.py`](../scripts/security/internal_ca_pki_provisioning.py)
- **Legacy entrypoint:** [`scripts/security/freeipa_pki_provisioning.py`](../scripts/security/freeipa_pki_provisioning.py) (re-exports the same CLI)

Environment variables:

- **`INTERNAL_CA_SERVER`** — base URL of the CA control plane (legacy alias: `FREEIPA_SERVER`)
- **`INTERNAL_CA_REALM`** — Kerberos realm for service principals (legacy: `FREEIPA_REALM`)

## Example compose / OpenTofu layout (optional)

If you maintain a security stack under `containers/security-stack/` or a separate OpenTofu/Kubernetes repo, keep **vendor-specific image names and YAML** in those layers only; prose in this repo should refer to **internal CA** and **OIDC IdP** in docs and runbooks. The default **qminiwasm-core** tree does not include `infra/opentofu/modules/`.

## Federation pattern

1. **mTLS** terminates on enclave ingress using certificates from the **internal CA**.
2. **OIDC** tokens assert **cognitive identity** (audience, `sub`, `jti`, short TTL) and are **orthogonal** to the CA-issued host/service identity.
3. **LDAP or directory** may back the IdP; treat it as an implementation detail of your IdP, not a hard dependency in application code.

## Operational commands (examples)

Health and federation checks depend on your CA and IdP products; use their CLIs or HTTP health endpoints. For IPA-style CAs, operator tools often expose `ipa healthcheck` inside the server container—adapt to your deployment.

---

**Note:** The former root-level summary file `FREEIPA_INTEGRATION_SUMMARY.md` redirects here to avoid vendor-specific naming in the primary narrative.
