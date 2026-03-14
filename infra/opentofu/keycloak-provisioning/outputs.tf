output "realm_id" {
  value       = keycloak_realm.qminiwasm.id
  description = "Keycloak realm id (same as realm name)"
}

output "admin_user_id" {
  value       = keycloak_user.admin_user.id
  description = "Admin test user id"
}

output "data_scientist_user_id" {
  value       = keycloak_user.data_scientist.id
  description = "Data-scientist test user id"
}
