output "ingress_nginx_namespace" {
  value       = helm_release.ingress_nginx.namespace
  description = "Namespace where NGINX Ingress is installed"
}

output "ingress_class_name" {
  value       = var.ingress_class_name
  description = "IngressClass name for Ingress resources"
}
