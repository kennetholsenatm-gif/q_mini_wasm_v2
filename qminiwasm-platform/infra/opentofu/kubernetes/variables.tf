# Variables for Teleport Helm release. No secrets here; use env or Vault for OIDC/audit config.

variable "kube_config_path" {
  description = "Path to kubeconfig (optional). Leave empty to use KUBECONFIG env or in-cluster config."
  type        = string
  default     = ""
}

variable "teleport_release_name" {
  description = "Helm release name for Teleport."
  type        = string
  default     = "teleport"
}

variable "teleport_namespace" {
  description = "Kubernetes namespace for Teleport."
  type        = string
  default     = "teleport"
}

variable "teleport_chart_version" {
  description = "Teleport Helm chart version (e.g. 16.1.0). Pin to match your Teleport version."
  type        = string
}

variable "teleport_values_file" {
  description = "Path to Helm values file for teleport-cluster (relative to repo root or absolute)."
  type        = string
}
