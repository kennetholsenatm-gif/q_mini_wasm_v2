# OpenTofu: deploy Kyverno and policies for admission control (deploy gates).
# Run: tofu -chdir=infra/opentofu/kyverno init && tofu -chdir=infra/opentofu/kyverno plan -var-file=../../desired/kyverno-main.tfvars.json
# CI runs when desired/kyverno-*.tfvars.json changes.

terraform {
  required_version = ">= 1.0"
  required_providers {
    kubernetes = { source = "hashicorp/kubernetes", version = "~> 2.23" }
    helm       = { source = "hashicorp/helm", version = "~> 2.11" }
  }
}

provider "kubernetes" {
  config_path = var.kube_config_path != "" ? var.kube_config_path : null
}

provider "helm" {
  kubernetes {
    config_path = var.kube_config_path != "" ? var.kube_config_path : null
  }
}

resource "helm_release" "kyverno" {
  repository       = "https://kyverno.github.io/kyverno"
  chart            = "kyverno"
  name             = var.kyverno_release_name
  namespace        = var.kyverno_namespace
  create_namespace = true
  version          = var.kyverno_chart_version
  values           = [file(var.kyverno_values_file)]
  wait             = true
}

# Apply ClusterPolicies after Kyverno CRDs exist (STIG baseline + image scan gate)
resource "kubernetes_manifest" "policy_stig" {
  depends_on = [helm_release.kyverno]
  manifest   = yamldecode(file("${path.module}/../../kyverno/policies/pod-security-stig.yaml"))
}

resource "kubernetes_manifest" "policy_image_scan" {
  depends_on = [helm_release.kyverno]
  manifest   = yamldecode(file("${path.module}/../../kyverno/policies/image-scan-gate.yaml"))
}
