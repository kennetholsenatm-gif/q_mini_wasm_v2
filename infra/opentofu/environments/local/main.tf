# Local environment: addons (NGINX Ingress) + workloads (Teleport, optional WUI).
# Cluster is created separately via modules/cluster/kind (Makefile or create-cluster.sh).

module "addons" {
  source = "../../modules/addons"

  ingress_nginx_namespace  = "ingress-nginx"
  ingress_nginx_chart_version = var.ingress_nginx_chart_version
  ingress_class_name       = var.ingress_class_name
  host_port_enabled        = true
}

module "workloads" {
  source = "../../modules/workloads"

  teleport_chart_version = var.teleport_chart_version
  teleport_values_file  = var.teleport_values_file
  enable_wui            = var.enable_wui
  wui_chart_path        = var.wui_chart_path != "" ? var.wui_chart_path : abspath("${path.module}/../../../../charts/qminiwasm-wui")
  wui_ingress_class     = var.ingress_class_name
}
