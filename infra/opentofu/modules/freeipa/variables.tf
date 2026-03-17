# FreeIPA Module Variables

# Basic Configuration
variable "freeipa_server_name" {
  description = "Name of the FreeIPA server container"
  type        = string
  default     = "security-stack-freeipa-server"
}

variable "freeipa_client_name" {
  description = "Name of the FreeIPA client container"
  type        = string
  default     = "security-stack-freeipa-client"
}

variable "freeipa_image" {
  description = "FreeIPA server Docker image"
  type        = string
  default     = "freeipa/freeipa-server:fedora-39"
}

variable "freeipa_client_image" {
  description = "FreeIPA client Docker image"
  type        = string
  default     = "freeipa/freeipa-client:fedora-39"
}

variable "freeipa_version" {
  description = "FreeIPA version"
  type        = string
  default     = "fedora-39"
}

# Domain and Realm Configuration
variable "freeipa_domain" {
  description = "FreeIPA domain name"
  type        = string
  default     = "qminiwasm.local"
}

variable "freeipa_realm" {
  description = "FreeIPA realm name"
  type        = string
  default     = "QMINIWASM.LOCAL"
}

variable "freeipa_server_hostname" {
  description = "FreeIPA server hostname"
  type        = string
  default     = "ipa.qminiwasm.local"
}

# Administrative Credentials
variable "freeipa_admin_password" {
  description = "FreeIPA admin password"
  type        = string
  sensitive   = true
}

variable "freeipa_ds_password" {
  description = "FreeIPA Directory Manager password"
  type        = string
  sensitive   = true
}

# Network Configuration
variable "freeipa_network_name" {
  description = "FreeIPA network name"
  type        = string
  default     = "edge-network"
}

variable "freeipa_network_subnet" {
  description = "FreeIPA network subnet"
  type        = string
  default     = "172.20.0.0/16"
}

variable "freeipa_network_gateway" {
  description = "FreeIPA network gateway"
  type        = string
  default     = "172.20.0.1"
}

variable "freeipa_dns_servers" {
  description = "DNS servers for FreeIPA"
  type        = list(string)
  default     = ["127.0.0.11", "8.8.8.8", "8.8.4.4"]
}

variable "freeipa_dns_forwarders" {
  description = "DNS forwarders for FreeIPA"
  type        = list(string)
  default     = ["8.8.8.8", "8.8.4.4"]
}

variable "freeipa_ntp_servers" {
  description = "NTP servers for FreeIPA"
  type        = list(string)
  default     = ["0.pool.ntp.org", "1.pool.ntp.org"]
}

# Service Ports
variable "freeipa_http_port" {
  description = "FreeIPA HTTP port"
  type        = number
  default     = 8080
}

variable "freeipa_https_port" {
  description = "FreeIPA HTTPS port"
  type        = number
  default     = 8443
}

variable "freeipa_ldap_port" {
  description = "FreeIPA LDAP port"
  type        = number
  default     = 389
}

variable "freeipa_ldaps_port" {
  description = "FreeIPA LDAPS port"
  type        = number
  default     = 636
}

variable "freeipa_kdc_port" {
  description = "FreeIPA Kerberos KDC port"
  type        = number
  default     = 88
}

variable "freeipa_kpasswd_port" {
  description = "FreeIPA Kerberos password change port"
  type        = number
  default     = 464
}

variable "freeipa_dns_port" {
  description = "FreeIPA DNS port"
  type        = number
  default     = 53
}

variable "freeipa_ntp_port" {
  description = "FreeIPA NTP port"
  type        = number
  default     = 123
}

# PKI Configuration
variable "freeipa_ca_subject" {
  description = "FreeIPA CA subject"
  type        = string
  default     = "CN=QMiniWASM IPA CA,O=QMINIWASM.LOCAL"
}

# Volume Configuration
variable "freeipa_data_volume_name" {
  description = "FreeIPA data volume name"
  type        = string
  default     = "freeipa-data"
}

variable "freeipa_cache_volume_name" {
  description = "FreeIPA cache volume name"
  type        = string
  default     = "freeipa-cache"
}

variable "freeipa_sssd_volume_name" {
  description = "FreeIPA SSSD volume name"
  type        = string
  default     = "freeipa-sssd"
}

variable "freeipa_client_data_volume_name" {
  description = "FreeIPA client data volume name"
  type        = string
  default     = "freeipa-client-data"
}

variable "freeipa_client_cache_volume_name" {
  description = "FreeIPA client cache volume name"
  type        = string
  default     = "freeipa-client-cache"
}

# Resource Limits
variable "freeipa_memory_limit" {
  description = "FreeIPA server memory limit"
  type        = string
  default     = "2G"
}

variable "freeipa_cpu_limit" {
  description = "FreeIPA server CPU limit"
  type        = string
  default     = "1.0"
}

variable "freeipa_memory_request" {
  description = "FreeIPA server memory request"
  type        = string
  default     = "1G"
}

variable "freeipa_cpu_request" {
  description = "FreeIPA server CPU request"
  type        = string
  default     = "0.5"
}

variable "freeipa_client_memory_limit" {
  description = "FreeIPA client memory limit"
  type        = string
  default     = "512M"
}

variable "freeipa_client_cpu_limit" {
  description = "FreeIPA client CPU limit"
  type        = string
  default     = "0.5"
}

variable "freeipa_client_memory_request" {
  description = "FreeIPA client memory request"
  type        = string
  default     = "256M"
}

variable "freeipa_client_cpu_request" {
  description = "FreeIPA client CPU request"
  type        = string
  default     = "0.25"
}

# Kubernetes Configuration
variable "enable_kubernetes" {
  description = "Enable Kubernetes deployment"
  type        = bool
  default     = false
}

variable "kubernetes_namespace" {
  description = "Kubernetes namespace for FreeIPA"
  type        = string
  default     = "security-stack"
}

variable "kubernetes_service_account" {
  description = "Kubernetes service account for FreeIPA"
  type        = string
  default     = "freeipa-service-account"
}

variable "freeipa_data_pvc_name" {
  description = "Kubernetes PVC name for FreeIPA data"
  type        = string
  default     = "freeipa-data-pvc"
}

variable "freeipa_cache_pvc_name" {
  description = "Kubernetes PVC name for FreeIPA cache"
  type        = string
  default     = "freeipa-cache-pvc"
}

variable "freeipa_sssd_pvc_name" {
  description = "Kubernetes PVC name for FreeIPA SSSD"
  type        = string
  default     = "freeipa-sssd-pvc"
}

# Integration Configuration
variable "keycloak_integration_enabled" {
  description = "Enable Keycloak integration"
  type        = bool
  default     = true
}

variable "vault_integration_enabled" {
  description = "Enable Vault integration"
  type        = bool
  default     = true
}

variable "envoy_integration_enabled" {
  description = "Enable Envoy integration"
  type        = bool
  default     = true
}

# Security Configuration
variable "enable_dns_updates" {
  description = "Enable DNS updates"
  type        = bool
  default     = true
}

variable "enable_ssh_keys" {
  description = "Enable SSH keys"
  type        = bool
  default     = true
}

variable "enable_ssh_hostkeys" {
  description = "Enable SSH host keys"
  type        = bool
  default     = true
}

variable "enable_compatibility_mode" {
  description = "Enable compatibility mode"
  type        = bool
  default     = true
}

variable "enable_migration" {
  description = "Enable migration mode"
  type        = bool
  default     = true
}

# Edge Environment Configuration
variable "edge_gateway_subnet" {
  description = "Subnet for edge gateways"
  type        = string
  default     = "172.20.0.0/16"
}

variable "quantum_router_hostname" {
  description = "Hostname for quantum router"
  type        = string
  default     = "router.qminiwasm.local"
}

variable "edge_gateway_hostname_template" {
  description = "Template for edge gateway hostnames"
  type        = string
  default     = "gateway{num}.edge.qminiwasm.local"
}

# Monitoring and Health Check Configuration
variable "health_check_interval" {
  description = "Health check interval"
  type        = string
  default     = "30s"
}

variable "health_check_timeout" {
  description = "Health check timeout"
  type        = string
  default     = "10s"
}

variable "health_check_retries" {
  description = "Health check retries"
  type        = number
  default     = 5
}

variable "health_check_start_period" {
  description = "Health check start period"
  type        = string
  default     = "120s"
}

# Backup Configuration
variable "enable_backup" {
  description = "Enable automatic backups"
  type        = bool
  default     = true
}

variable "backup_schedule" {
  description = "Backup schedule (cron format)"
  type        = string
  default     = "0 2 * * *"
}

variable "backup_retention_days" {
  description = "Backup retention period in days"
  type        = number
  default     = 30
}

# Logging Configuration
variable "log_level" {
  description = "FreeIPA log level"
  type        = string
  default     = "info"
  validation {
    condition     = contains(["debug", "info", "warn", "error"], var.log_level)
    error_message = "Log level must be one of: debug, info, warn, error."
  }
}

variable "log_format" {
  description = "FreeIPA log format"
  type        = string
  default     = "json"
  validation {
    condition     = contains(["json", "text"], var.log_format)
    error_message = "Log format must be either 'json' or 'text'."
  }
}