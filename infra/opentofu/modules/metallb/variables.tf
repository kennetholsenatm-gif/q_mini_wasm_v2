variable "metallb_chart_version" {
  type        = string
  default     = "0.15.3"
  description = "MetalLB Helm chart version"
}

variable "address_pool" {
  type        = list(string)
  description = "IP address pool for LoadBalancer services (e.g. [\"172.18.255.200-172.18.255.220\"] for Kind Docker network, or [\"192.168.1.200-192.168.1.220\"] for DHCP subnet; reserve range in DHCP)"
}

variable "pool_name" {
  type        = string
  default     = "default-pool"
  description = "Name of the IPAddressPool and L2Advertisement resources"
}
