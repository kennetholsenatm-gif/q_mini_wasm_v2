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

variable "teleport_timeout" {
  type        = number
  default     = 900
  description = "Helm timeout in seconds for Teleport (local dev: 15 min)"
}

variable "teleport_wait_for_jobs" {
  type        = bool
  default     = false
  description = "Wait for Teleport Helm hook jobs. False for local when OIDC connector is not created yet."
}

variable "teleport_wait" {
  type        = bool
  default     = false
  description = "Wait for Teleport pods to be ready. False for local so local-up completes; fix Teleport (e.g. OIDC) and restart pods afterward."
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

variable "metallb_address_pool" {
  type        = list(string)
  default     = ["172.18.255.200-172.18.255.220"]
  description = "MetalLB IP address pool for LoadBalancer services. Default fits Kind Docker network; for DHCP subnet use e.g. [\"192.168.1.200-192.168.1.220\"] and reserve that range in DHCP."
}
