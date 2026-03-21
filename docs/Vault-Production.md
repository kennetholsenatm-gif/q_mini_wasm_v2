# Vault Production Configuration

This runbook describes how to run HashiCorp Vault in **production** instead of dev mode: seal configuration, unseal flow, token/AppRole policy, and rotating secrets. The default [containers/security-stack](../containers/security-stack/) Compose uses **Vault dev mode** (single root token, in-memory storage); do not use that in production.

## Production requirements

- **Seal:** Use a proper seal so Vault can persist and recover. Options: **file** (single node), **Transit** (Vault-as-a-service or dedicated Vault cluster), or **cloud KMS** (AWS KMS, GCP Cloud KMS, Azure Key Vault).
- **No dev root token:** Do not use the dev root token (`VAULT_DEV_ROOT_TOKEN_ID=root`) in production. Use short-lived tokens, AppRole, or JWT auth tied to Keycloak.
- **Secrets not in repo:** Do not commit `.env` or any file containing root tokens or unseal keys. Use a secrets manager or secure distribution for initial unseal and AppRole IDs/secrets.

## Seal configuration

### Option 1: File seal (single node)

Suitable for a single Vault instance. Unseal keys are written to a file (or split across operators).

1. Create a Vault config file (e.g. `vault-production.hcl`):

   ```hcl
   storage "file" {
     path = "/vault/data"
   }
   listener "tcp" {
     address     = "0.0.0.0:8200"
     tls_disable = 1
   }
   seal "shamir" {}
   disable_mlock = true
   ```

2. Start Vault with this config: `vault server -config=vault-production.hcl`.
3. Initialize once: `vault operator init`. Store the unseal keys and root token securely; you will need the unseal keys after every restart.
4. Unseal: `vault operator unseal` (provide key shares per your threshold).

In Docker, mount a volume for `/vault/data`, run Vault with the production config (not `-dev`), and run `vault operator init` once and `vault operator unseal` after each start. You can use a Compose override that replaces the dev-mode `command` with the config file and a suitable entrypoint.

### Option 2: Transit seal (auto-unseal)

Vault can use another Vault cluster (or the same cluster’s Transit engine) as the seal. Unseal is automatic on restart.

1. In the **seal Vault** (e.g. HCP Vault or a dedicated cluster), enable Transit and create a key:  
   `vault write -f transit/keys/vault-seal`.
2. Create a policy that allows the **sealed Vault** to use that key for encrypt/decrypt.
3. In the **sealed Vault** config:

   ```hcl
   seal "transit" {
     address       = "https://seal-vault.example.com"
     token         = "s.xxx"
     key_name      = "vault-seal"
     mount_path    = "transit/"
   }
   ```

4. Use a dedicated token with minimal policy (only Transit encrypt/decrypt for `vault-seal`). Rotate that token via your secrets process; do not commit it.

### Option 3: Cloud KMS (auto-unseal)

Use AWS KMS, GCP Cloud KMS, or Azure Key Vault as the seal. See HashiCorp docs:

- [AWS KMS auto-unseal](https://developer.hashicorp.com/vault/docs/configuration/seal/awskms)
- [GCP Cloud KMS](https://developer.hashicorp.com/vault/docs/configuration/seal/gcpckms)
- [Azure Key Vault](https://developer.hashicorp.com/vault/docs/configuration/seal/azurekeyvault)

Configure the seal block in Vault’s config and provide credentials (IAM role, service account, or managed identity). Unseal is automatic.

## Unseal flow

- **Shamir (file seal):** After each Vault restart, run `vault operator unseal` and provide the required number of key shares. Optionally script this from a secure bastion; never put unseal keys in Git.
- **Transit / cloud KMS:** Unseal is automatic once the seal backend is reachable and authorized.

## Token and AppRole policy

1. **Root token:** Use only for initial setup. Store offline or in a break-glass process. Do not put in `.env` or CI.
2. **AppRole for services:** Create an AppRole (e.g. `pki-consumer`) with policy that allows `read` on PKI issue paths (e.g. `pki_int/issue/postgres-client`). Services (NiFi, WUI backend) use RoleID + SecretID to get a short-lived token, then request certs.
3. **Policy example** (minimal for PKI issue):

   ```hcl
   path "pki_int/issue/postgres-client" {
     capabilities = [ "create", "update" ]
   }
   ```

4. **JWT auth (Keycloak):** For human or service identity from Keycloak, enable `jwt` auth in Vault and configure it with Keycloak’s JWKS. Map Keycloak roles to Vault policies. Then use `vault write auth/jwt/login role=... jwt=...` with a token from Keycloak.

## Rotating VAULT_* and secrets

- **Root token:** If you must rotate, generate a new root token with `vault operator generate-root` (and revoke the old one). Prefer not using root at all in normal operation.
- **AppRole SecretID:** Rotate by generating a new SecretID and updating the service’s secret store (e.g. Kubernetes Secret, Vault Agent injector). Old SecretIDs can be revoked.
- **Transit/KMS seal token or keys:** Rotate according to your cloud/KMS and HashiCorp docs. Update Vault config and restart; ensure the new credential has the same seal permissions during transition.
- **`.env` in production:** Do not store root token or unseal keys in `.env`. Use environment injection from a secrets manager (e.g. Vault Agent, cloud secret manager) for AppRole RoleID/SecretID or JWT only.

## Compose override (example, file seal)

You can add `docker-compose.production.yml` (or similar) that overrides the Vault service to use a config file and file storage instead of dev mode. Example idea:

- Volume for `/vault/data`.
- Command: `vault server -config=/vault/config/config.hcl`.
- Copy or mount a `config.hcl` that uses `storage "file"` and `seal "shamir"` (no `-dev`).
- After first start, run `vault operator init` once from a job or manually; then on each start run `vault operator unseal` (or use an init container that unseals when using Transit/KMS).

Do not commit `config.hcl` if it contains any secrets; use env substitution or a secure mount for tokens.

## References

- [HashiCorp Vault: Production hardening](https://developer.hashicorp.com/vault/docs/internals/security)
- [Seal configuration](https://developer.hashicorp.com/vault/docs/configuration/seal)
- [AppRole](https://developer.hashicorp.com/vault/docs/auth/approle)
- [PKI secrets engine](https://developer.hashicorp.com/vault/docs/secrets/pki)
