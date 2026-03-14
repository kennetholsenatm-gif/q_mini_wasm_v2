output "teleport_namespace" {
  value       = helm_release.teleport.namespace
  description = "Namespace where Teleport is installed"
}

output "wui_namespace" {
  value       = var.enable_wui ? var.wui_namespace : null
  description = "Namespace where WUI is installed (if enable_wui)"
}
