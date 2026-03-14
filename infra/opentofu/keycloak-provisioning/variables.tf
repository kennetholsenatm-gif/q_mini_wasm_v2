# Keycloak provisioning — no default credentials. Use TF_VAR_* env or a .tfvars file in .gitignore.

variable "keycloak_url" {
  type        = string
  description = "Keycloak base URL (e.g. https://keycloak.example.com or http://localhost:8080)"
}

variable "keycloak_initial_admin_username" {
  type        = string
  description = "Keycloak admin username (initial admin user)"
}

variable "keycloak_initial_admin_password" {
  type        = string
  sensitive   = true
  description = "Keycloak admin password (initial admin user)"
}

variable "realm_name" {
  type        = string
  default     = "qminiwasm"
  description = "Realm name to create or manage"
}

# Optional: for provider auth via client (machine-to-machine)
variable "keycloak_client_id" {
  type        = string
  default     = "admin-cli"
  description = "Client ID for provider authentication"
}

variable "keycloak_client_secret" {
  type        = string
  default     = null
  sensitive   = true
  description = "Client secret when using client credentials; null for password grant"
}

variable "admin_user_username" {
  type        = string
  default     = "admin-user"
  description = "Test admin user username"
}

variable "admin_user_initial_password" {
  type        = string
  default     = null
  sensitive   = true
  description = "Initial password for admin test user (set via TF_VAR_admin_user_initial_password or tfvars)"
}

variable "data_scientist_username" {
  type        = string
  default     = "data-scientist"
  description = "Test data-scientist user username"
}

variable "data_scientist_initial_password" {
  type        = string
  default     = null
  sensitive   = true
  description = "Initial password for data-scientist test user"
}
