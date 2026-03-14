# OpenTofu: deploy Teleport via Helm on an existing Kubernetes cluster.
# Run from repo root: tofu -chdir=infra/opentofu/kubernetes init && tofu -chdir=infra/opentofu/kubernetes plan -var-file=../../desired/kubernetes-teleport.tfvars.json
# CI runs this when desired/*.tfvars.json changes (see .github/workflows/opentofu-infra.yml).

terraform {
  required_version = ">= 1.0"
  required_providers {
    kubernetes = {
      source  = "hashicorp/kubernetes"
      version = "~> 2.23"
    }
    helm = {
      source  = "hashicorp/helm"
      version = "~> 2.11"
    }
  }
}

provider "kubernetes" {
  config_path = var.kube_config_path != "" ? var.kube_config_path : null
  # When config_path is null, provider uses KUBECONFIG env or in-cluster config.
}

provider "helm" {
  kubernetes {
    config_path = var.kube_config_path != "" ? var.kube_config_path : null
  }
}

resource "helm_release" "teleport" {
  repository       = "https://charts.releases.teleport.dev"
  chart            = "teleport-cluster"
  name             = var.teleport_release_name
  namespace        = var.teleport_namespace
  create_namespace  = true
  version          = var.teleport_chart_version
  values           = [file(var.teleport_values_file)]
  wait             = true
  wait_for_jobs    = true
}
