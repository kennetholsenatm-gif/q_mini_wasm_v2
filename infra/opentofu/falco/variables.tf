variable "kube_config_path" {
  description = "Path to kubeconfig. Empty to use KUBECONFIG or in-cluster."
  type        = string
  default     = ""
}

variable "falco_release_name" {
  type    = string
  default = "falco"
}

variable "falco_namespace" {
  type    = string
  default = "falco"
}

variable "falco_chart_version" {
  type = string
}

variable "falco_values_file" {
  description = "Path to Falco Helm values file."
  type        = string
}
