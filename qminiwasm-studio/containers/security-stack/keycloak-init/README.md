# Keycloak realm import

## Auto-import on startup

With `--import-realm`, Keycloak imports any `*-realm.json` from `/opt/keycloak/data/import` (mounted from `keycloak-init/`). The file **must** be named `<realm>-realm.json` (e.g. `qminiwasm-realm.json` for realm `qminiwasm`).

- **qminiwasm-realm.json** defines the `qminiwasm` realm with:
  - OIDC client `teleport` for Teleport IdP (set redirect URIs and client secret in Admin UI).
  - WebAuthn passwordless policy and required action `webauthn-register-passwordless`.
  - Realm roles: `user`, `dev-read-only`, `dev-admin`.

If the realm already exists, import is skipped (no overwrite on restart).

## Manual setup (alternative)

1. Start the stack without a realm file and open Keycloak Admin: `http://localhost:8080` (or via Envoy).
2. Create realm `qminiwasm`, create OIDC client `teleport` with your Teleport callback URLs and client secret.
3. In Authentication → Flows, configure the browser flow to require WebAuthn (passwordless) as second factor; or use a passwordless-only flow.
4. In Realm settings → WebAuthn Passwordless Policy, set Relying Party Entity Name (e.g. "Q-Mini-WASM") and user verification requirement.
5. Export the realm: Admin Console → Realm settings → Partial export, or CLI: `kc.sh export --dir /tmp/export --realm qminiwasm`.
6. Copy `qminiwasm-realm.json` into `keycloak-init/` for future imports.

## Teleport OIDC

Configure Teleport's OIDC connector with:

- Issuer URL: `http://keycloak:8080/realms/qminiwasm` (or the public URL behind Envoy).
- Client ID: `teleport`.
- Client secret: set in Keycloak client → Credentials.
- Redirect URL: your Teleport callback (e.g. `https://teleport.example.com/callback`).
- Claims mapping: map IdP groups to Teleport roles (e.g. `qminiwasm-dev-admin` → `dev-admin`).
