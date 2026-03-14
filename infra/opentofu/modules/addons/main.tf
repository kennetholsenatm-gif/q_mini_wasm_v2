# NGINX Ingress Controller for local Kind or any cluster.
# For Kind: hostPort 80/443 so host traffic reaches controller; use extraPortMappings in Kind config.
resource "helm_release" "ingress_nginx" {
  repository       = "https://kubernetes.github.io/ingress-nginx"
  chart            = "ingress-nginx"
  name             = "ingress-nginx"
  namespace        = var.ingress_nginx_namespace
  create_namespace = true
  version          = var.ingress_nginx_chart_version
  wait             = true
  wait_for_jobs    = true

  values = [
    yamlencode({
      controller = {
        ingressClassResource = {
          name = var.ingress_class_name
          enabled = true
          default = true
        }
        hostPort = {
          enabled = var.host_port_enabled
          ports = {
            http  = 80
            https = 443
          }
        }
        service = {
          type = "NodePort"
        }
      }
    })
  ]
}
