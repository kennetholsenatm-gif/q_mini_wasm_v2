output "ingress_class" {
  value       = module.addons.ingress_class_name
  description = "IngressClass name for Ingress resources"
}

output "ingress_nginx_namespace" {
  value       = module.addons.ingress_nginx_namespace
  description = "Namespace of NGINX Ingress Controller"
}

output "teleport_namespace" {
  value       = module.workloads.teleport_namespace
  description = "Namespace where Teleport is installed"
}

output "kubeconfig_path" {
  value       = var.kube_config_path != "" ? var.kube_config_path : "KUBECONFIG env"
  description = "Kubeconfig in use for this run"
}

output "ingress_local_url" {
  value       = "http://localhost (and https://localhost if TLS configured)"
  description = "Local ingress base URL after cluster and addons are up"
}
