# Kubernetes and Helm providers. Use KUBECONFIG env or kube_config_path variable.
# After creating the Kind cluster, point to environments/local/kubeconfig or export KUBECONFIG.

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
}

provider "helm" {
  kubernetes {
    config_path = var.kube_config_path != "" ? var.kube_config_path : null
  }
}
