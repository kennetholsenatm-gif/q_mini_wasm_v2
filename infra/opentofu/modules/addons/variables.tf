variable "ingress_nginx_namespace" {
  type        = string
  default     = "ingress-nginx"
  description = "Namespace for NGINX Ingress Controller"
}

variable "ingress_nginx_chart_version" {
  type        = string
  default     = "4.11.0"
  description = "Helm chart version for ingress-nginx"
}

variable "ingress_class_name" {
  type        = string
  default     = "nginx"
  description = "IngressClass name (used by Ingress resources)"
}

variable "host_port_enabled" {
  type        = bool
  default     = true
  description = "Bind controller to host ports 80/443 (for Kind); set false for cloud LB"
}
