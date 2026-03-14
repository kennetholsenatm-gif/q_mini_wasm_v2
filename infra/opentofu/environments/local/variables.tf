variable "kube_config_path" {
  type        = string
  default     = ""
  description = "Path to kubeconfig. Empty = use KUBECONFIG env. For Kind use ./kubeconfig after make kind-kubeconfig"
}

variable "ingress_class_name" {
  type        = string
  default     = "nginx"
  description = "IngressClass name for addons and workloads"
}

variable "ingress_nginx_chart_version" {
  type        = string
  default     = "4.11.0"
  description = "Helm chart version for ingress-nginx"
}

variable "teleport_chart_version" {
  type        = string
  description = "Teleport Helm chart version (e.g. 16.1.0)"
}

variable "teleport_values_file" {
  type        = string
  description = "Path to Teleport values file (relative to this dir or absolute)"
}

variable "enable_wui" {
  type        = bool
  default     = false
  description = "Deploy qminiwasm-wui chart"
}

variable "wui_chart_path" {
  type        = string
  default     = ""
  description = "Path to qminiwasm-wui chart (required when enable_wui is true)"
}
