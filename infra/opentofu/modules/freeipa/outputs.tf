# FreeIPA Module Outputs

# Container Outputs
output "freeipa_server_container_id" {
  description = "FreeIPA server container ID"
  value       = docker_container.freeipa_server.id
  sensitive   = false
}

output "freeipa_client_container_id" {
  description = "FreeIPA client container ID"
  value       = docker_container.freeipa_client.id
  sensitive   = false
}

# Network Outputs
output "freeipa_network_id" {
  description = "FreeIPA network ID"
  value       = docker_network.freeipa_network.id
  sensitive   = false
}

output "freeipa_network_name" {
  description = "FreeIPA network name"
  value       = var.freeipa_network_name
  sensitive   = false
}

output "freeipa_network_subnet" {
  description = "FreeIPA network subnet"
  value       = var.freeipa_network_subnet
  sensitive   = false
}

# Volume Outputs
output "freeipa_data_volume_id" {
  description = "FreeIPA data volume ID"
  value       = docker_volume.freeipa_data.id
  sensitive   = false
}

output "freeipa_cache_volume_id" {
  description = "FreeIPA cache volume ID"
  value       = docker_volume.freeipa_cache.id
  sensitive   = false
}

output "freeipa_sssd_volume_id" {
  description = "FreeIPA SSSD volume ID"
  value       = docker_volume.freeipa_sssd.id
  sensitive   = false
}

output "freeipa_client_data_volume_id" {
  description = "FreeIPA client data volume ID"
  value       = docker_volume.freeipa_client_data.id
  sensitive   = false
}

output "freeipa_client_cache_volume_id" {
  description = "FreeIPA client cache volume ID"
  value       = docker_volume.freeipa_client_cache.id
  sensitive   = false
}

# Service Endpoints
output "freeipa_web_ui_url" {
  description = "FreeIPA Web UI URL"
  value       = "http://${var.freeipa_server_hostname}:${var.freeipa_http_port}"
  sensitive   = false
}

output "freeipa_https_url" {
  description = "FreeIPA HTTPS URL"
  value       = "https://${var.freeipa_server_hostname}:${var.freeipa_https_port}"
  sensitive   = false
}

output "freeipa_ldap_url" {
  description = "FreeIPA LDAP URL"
  value       = "ldap://${var.freeipa_server_hostname}:${var.freeipa_ldap_port}"
  sensitive   = false
}

output "freeipa_ldaps_url" {
  description = "FreeIPA LDAPS URL"
  value       = "ldaps://${var.freeipa_server_hostname}:${var.freeipa_ldaps_port}"
  sensitive   = false
}

output "freeipa_kerberos_url" {
  description = "FreeIPA Kerberos URL"
  value       = "krb5://${var.freeipa_server_hostname}:${var.freeipa_kdc_port}"
  sensitive   = false
}

# Domain and Realm Information
output "freeipa_domain" {
  description = "FreeIPA domain"
  value       = var.freeipa_domain
  sensitive   = false
}

output "freeipa_realm" {
  description = "FreeIPA realm"
  value       = var.freeipa_realm
  sensitive   = false
}

output "freeipa_server_hostname" {
  description = "FreeIPA server hostname"
  value       = var.freeipa_server_hostname
  sensitive   = false
}

# Administrative Information
output "freeipa_admin_user" {
  description = "FreeIPA admin user"
  value       = "admin"
  sensitive   = false
}

output "freeipa_admin_password" {
  description = "FreeIPA admin password"
  value       = var.freeipa_admin_password
  sensitive   = true
}

output "freeipa_ds_user" {
  description = "FreeIPA Directory Manager user"
  value       = "cn=Directory Manager"
  sensitive   = false
}

output "freeipa_ds_password" {
  description = "FreeIPA Directory Manager password"
  value       = var.freeipa_ds_password
  sensitive   = true
}

# Integration Information
output "keycloak_ldap_connection_url" {
  description = "Keycloak LDAP connection URL for FreeIPA"
  value       = "ldap://${var.freeipa_server_hostname}:${var.freeipa_ldap_port}"
  sensitive   = false
}

output "keycloak_ldap_users_dn" {
  description = "Keycloak LDAP users DN for FreeIPA"
  value       = "cn=users,cn=accounts,dc=${replace(var.freeipa_domain, ".", ",dc=")}"
  sensitive   = false
}

output "keycloak_ldap_bind_dn" {
  description = "Keycloak LDAP bind DN for FreeIPA"
  value       = "uid=admin,cn=users,cn=accounts,dc=${replace(var.freeipa_domain, ".", ",dc=")}"
  sensitive   = false
}

output "vault_pki_ca_url" {
  description = "Vault PKI CA URL for FreeIPA"
  value       = "https://${var.freeipa_server_hostname}:${var.freeipa_https_port}/ca/ascii"
  sensitive   = false
}

# Kubernetes Outputs (when enabled)
output "kubernetes_deployment_name" {
  description = "Kubernetes deployment name for FreeIPA"
  value       = var.enable_kubernetes ? kubernetes_deployment.freeipa_server[0].metadata[0].name : null
  sensitive   = false
}

output "kubernetes_service_name" {
  description = "Kubernetes service name for FreeIPA"
  value       = var.enable_kubernetes ? kubernetes_service.freeipa_server[0].metadata[0].name : null
  sensitive   = false
}

output "kubernetes_service_type" {
  description = "Kubernetes service type for FreeIPA"
  value       = var.enable_kubernetes ? "LoadBalancer" : null
  sensitive   = false
}

output "kubernetes_service_external_ip" {
  description = "Kubernetes service external IP for FreeIPA"
  value       = var.enable_kubernetes ? kubernetes_service.freeipa_server[0].status[0].load_balancer[0].ingress[0].ip : null
  sensitive   = false
}

# Security Information
output "freeipa_ca_subject" {
  description = "FreeIPA CA subject"
  value       = var.freeipa_ca_subject
  sensitive   = false
}

output "freeipa_certificate_validity_days" {
  description = "FreeIPA certificate validity days"
  value       = 365
  sensitive   = false
}

output "freeipa_certificate_key_size" {
  description = "FreeIPA certificate key size"
  value       = 2048
  sensitive   = false
}

# Edge Environment Information
output "edge_gateway_subnet" {
  description = "Edge gateway subnet"
  value       = var.edge_gateway_subnet
  sensitive   = false
}

output "quantum_router_hostname" {
  description = "Quantum router hostname"
  value       = var.quantum_router_hostname
  sensitive   = false
}

output "edge_gateway_hostname_template" {
  description = "Edge gateway hostname template"
  value       = var.edge_gateway_hostname_template
  sensitive   = false
}

# Monitoring and Health Check Information
output "health_check_interval" {
  description = "Health check interval"
  value       = var.health_check_interval
  sensitive   = false
}

output "health_check_timeout" {
  description = "Health check timeout"
  value       = var.health_check_timeout
  sensitive   = false
}

output "health_check_retries" {
  description = "Health check retries"
  value       = var.health_check_retries
  sensitive   = false
}

output "health_check_start_period" {
  description = "Health check start period"
  value       = var.health_check_start_period
  sensitive   = false
}

# Backup Information
output "backup_enabled" {
  description = "Backup enabled status"
  value       = var.enable_backup
  sensitive   = false
}

output "backup_schedule" {
  description = "Backup schedule"
  value       = var.backup_schedule
  sensitive   = false
}

output "backup_retention_days" {
  description = "Backup retention days"
  value       = var.backup_retention_days
  sensitive   = false
}

# Logging Information
output "log_level" {
  description = "FreeIPA log level"
  value       = var.log_level
  sensitive   = false
}

output "log_format" {
  description = "FreeIPA log format"
  value       = var.log_format
  sensitive   = false
}

# Integration Status
output "keycloak_integration_enabled" {
  description = "Keycloak integration enabled"
  value       = var.keycloak_integration_enabled
  sensitive   = false
}

output "vault_integration_enabled" {
  description = "Vault integration enabled"
  value       = var.vault_integration_enabled
  sensitive   = false
}

output "envoy_integration_enabled" {
  description = "Envoy integration enabled"
  value       = var.envoy_integration_enabled
  sensitive   = false
}

# Resource Information
output "freeipa_server_resources" {
  description = "FreeIPA server resource limits"
  value = {
    memory = var.freeipa_memory_limit
    cpu    = var.freeipa_cpu_limit
  }
  sensitive = false
}

output "freeipa_client_resources" {
  description = "FreeIPA client resource limits"
  value = {
    memory = var.freeipa_client_memory_limit
    cpu    = var.freeipa_client_cpu_limit
  }
  sensitive = false
}

# Module Metadata
output "module_version" {
  description = "FreeIPA module version"
  value       = "1.0.0"
  sensitive   = false
}

output "module_description" {
  description = "FreeIPA module description"
  value       = "FreeIPA Server Module for Hierarchical Edge-Quantum AI Architecture"
  sensitive   = false
}

output "module_author" {
  description = "FreeIPA module author"
  value       = "QMiniWASM DevSecOps Team"
  sensitive   = false
}