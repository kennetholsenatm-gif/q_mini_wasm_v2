variable "teleport_release_name" {
  type        = string
  default     = "teleport"
  description = "Helm release name for Teleport"
}

variable "teleport_namespace" {
  type        = string
  default     = "teleport"
  description = "Kubernetes namespace for Teleport"
}

variable "teleport_chart_version" {
  type        = string
  description = "Teleport Helm chart version (e.g. 16.1.0)"
}

variable "teleport_values_file" {
  type        = string
  description = "Path to Teleport Helm values file (absolute or relative to caller)"
}

variable "teleport_timeout" {
  type        = number
  default     = 600
  description = "Helm install/upgrade timeout in seconds for Teleport (default 10 min)"
}

variable "teleport_wait_for_jobs" {
  type        = bool
  default     = true
  description = "If true, wait for Helm hook jobs to complete. Set false for local dev when OIDC is not configured yet."
}

variable "teleport_wait" {
  type        = bool
  default     = true
  description = "If true, wait for Teleport pods to be ready. Set false for local dev to let local-up complete and fix Teleport (e.g. OIDC) afterward."
}

variable "enable_wui" {
  type        = bool
  default     = false
  description = "Deploy qminiwasm-wui Helm chart"
}

variable "wui_chart_path" {
  type        = string
  default     = ""
  description = "Path to qminiwasm-wui chart (required when enable_wui is true)"
}

variable "wui_namespace" {
  type        = string
  default     = "qminiwasm-wui"
  description = "Namespace for WUI deployment"
}

variable "wui_ingress_class" {
  type        = string
  default     = "nginx"
  description = "IngressClass for WUI ingress"
}
