variable "kube_config_path" {
  description = "Path to kubeconfig. Empty to use KUBECONFIG or in-cluster."
  type        = string
  default     = ""
}

variable "kyverno_release_name" {
  type    = string
  default = "kyverno"
}

variable "kyverno_namespace" {
  type    = string
  default = "kyverno"
}

variable "kyverno_chart_version" {
  type = string
}

variable "kyverno_values_file" {
  description = "Path to Helm values file (relative to this module or absolute)."
  type        = string
}
