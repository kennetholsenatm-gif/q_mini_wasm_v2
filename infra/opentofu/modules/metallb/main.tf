# MetalLB: LoadBalancer provider for Kind/bare metal. Assigns external IPs from address_pool via Layer 2.
# Helm installs MetalLB; IPAddressPool and L2Advertisement are CRs (create after Helm so CRDs exist).
# Namespace is created with pod-security labels so MetalLB speaker (privileged for ARP) is allowed.

resource "kubernetes_namespace" "metallb" {
  metadata {
    name = "metallb-system"
    labels = {
      "pod-security.kubernetes.io/enforce" = "privileged"
      "pod-security.kubernetes.io/audit"   = "privileged"
      "pod-security.kubernetes.io/warn"   = "privileged"
    }
  }
}

resource "helm_release" "metallb" {
  depends_on       = [kubernetes_namespace.metallb]
  repository       = "https://metallb.github.io/metallb"
  chart            = "metallb"
  name             = "metallb"
  namespace        = kubernetes_namespace.metallb.metadata[0].name
  create_namespace = false
  version          = var.metallb_chart_version
  wait             = true
}

resource "kubernetes_manifest" "ipaddress_pool" {
  depends_on = [helm_release.metallb]

  manifest = {
    apiVersion = "metallb.io/v1beta1"
    kind       = "IPAddressPool"
    metadata = {
      name      = var.pool_name
      namespace = "metallb-system"
    }
    spec = {
      addresses = var.address_pool
    }
  }
}

resource "kubernetes_manifest" "l2_advertisement" {
  depends_on = [kubernetes_manifest.ipaddress_pool]

  manifest = {
    apiVersion = "metallb.io/v1beta1"
    kind       = "L2Advertisement"
    metadata = {
      name      = var.pool_name
      namespace = "metallb-system"
    }
    spec = {
      ipAddressPools = [var.pool_name]
    }
  }
}
