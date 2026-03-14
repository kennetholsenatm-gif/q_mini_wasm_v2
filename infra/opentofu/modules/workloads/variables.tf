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
