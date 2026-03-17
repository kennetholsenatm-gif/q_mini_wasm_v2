# Teleport and optional WUI. Reuses infra/teleport/teleport-values.yaml and charts/qminiwasm-wui.
# No secrets in tfvars; use env or Vault for OIDC and audit.

resource "helm_release" "teleport" {
  repository        = "https://charts.releases.teleport.dev"
  chart             = "teleport-cluster"
  name              = var.teleport_release_name
  namespace         = var.teleport_namespace
  create_namespace  = true
  version           = var.teleport_chart_version
  values            = [file(var.teleport_values_file)]
  wait              = var.teleport_wait
  wait_for_jobs     = var.teleport_wait_for_jobs
  timeout           = var.teleport_timeout
}

resource "helm_release" "wui" {
  count = var.enable_wui && var.wui_chart_path != "" ? 1 : 0

  chart             = var.wui_chart_path
  name              = "qminiwasm-wui"
  namespace         = var.wui_namespace
  create_namespace  = true
  wait              = true
  wait_for_jobs     = true

  values = [
    yamlencode({
      ingress = {
        enabled     = true
        className   = var.wui_ingress_class
        hosts = [
          { host = "wui.local", paths = [{ path = "/", pathType = "Prefix" }] }
        ]
      }
    })
  ]
}
