terraform {
  required_providers {
    keycloak = {
      source  = "mrparkers/keycloak"
      version = "~> 4.0"
    }
  }
}

provider "keycloak" {
  url      = var.keycloak_url
  username = var.keycloak_initial_admin_username
  password = var.keycloak_initial_admin_password
  realm    = "master"
  client_id = var.keycloak_client_id
}

resource "keycloak_realm" "qminiwasm" {
  realm        = var.realm_name
  enabled      = true
  display_name = "Q-Mini WASM"

  login_with_email_allowed = true
  registration_allowed     = false
  reset_password_allowed   = true
  edit_username_allowed   = false
  duplicate_emails_allowed = false

  ssl_required = "external"
  brute_force_protected = true
  permanent_lockout     = false
}

resource "keycloak_group" "admin_user" {
  realm_id = keycloak_realm.qminiwasm.id
  name    = "admin-user"
  path    = "/admin-user"
}

resource "keycloak_group" "data_scientist" {
  realm_id = keycloak_realm.qminiwasm.id
  name    = "data-scientist"
  path    = "/data-scientist"
}

resource "keycloak_user" "admin_user" {
  realm_id = keycloak_realm.qminiwasm.id
  username = var.admin_user_username
  enabled  = true
  initial_password {
    value     = var.admin_user_initial_password != null ? var.admin_user_initial_password : "changeme"
    temporary = true
  }
}

resource "keycloak_user" "data_scientist" {
  realm_id = keycloak_realm.qminiwasm.id
  username = var.data_scientist_username
  enabled  = true
  initial_password {
    value     = var.data_scientist_initial_password != null ? var.data_scientist_initial_password : "changeme"
    temporary = true
  }
}

resource "keycloak_group_memberships" "admin_user" {
  realm_id = keycloak_realm.qminiwasm.id
  group_id = keycloak_group.admin_user.id
  members  = [keycloak_user.admin_user.id]
}

resource "keycloak_group_memberships" "data_scientist" {
  realm_id = keycloak_realm.qminiwasm.id
  group_id = keycloak_group.data_scientist.id
  members  = [keycloak_user.data_scientist.id]
}
