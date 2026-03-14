# Keycloak provisioning (OpenTofu)

This module manages the **qminiwasm** realm, groups (`admin-user`, `data-scientist`), and test users using the [mrparkers/keycloak](https://registry.terraform.io/providers/mrparkers/keycloak) provider. It is an **alternative** to importing the realm via JSON (`containers/security-stack/keycloak-init/qminiwasm-realm.json`).

## Prerequisites

- Keycloak must be running and reachable (e.g. start `containers/security-stack` first).
- OpenTofu or Terraform CLI (`tofu init`, `tofu plan`, `tofu apply`).

## Authentication

The provider uses the **master** realm admin user. Do **not** commit credentials.

1. Set variables via environment (recommended):
   - `TF_VAR_keycloak_url` — e.g. `http://localhost:8080`
   - `TF_VAR_keycloak_initial_admin_username` — admin username
   - `TF_VAR_keycloak_initial_admin_password` — admin password (sensitive)
2. Or use a `terraform.tfvars` / `*.auto.tfvars` file and add it to `.gitignore`.

## Usage

```bash
cd infra/opentofu/keycloak-provisioning
tofu init
export TF_VAR_keycloak_url="http://localhost:8080"
export TF_VAR_keycloak_initial_admin_username=admin
export TF_VAR_keycloak_initial_admin_password=your_admin_password
tofu plan
tofu apply
```

Optional: set `TF_VAR_admin_user_initial_password` and `TF_VAR_data_scientist_initial_password` for the test users; otherwise a default temporary password is used.

## Conflict with JSON import

If the security stack already imports `qminiwasm-realm.json`, either:

- Disable that import for the realm and use this module as the single source of truth for realm, groups, and users, or
- Use this module only to **add** users and groups after the initial realm creation via JSON (ensure the realm and group names match to avoid duplicates).

## Lock file

Run `tofu init` and commit `.terraform.lock.hcl` for reproducible provider versions.
