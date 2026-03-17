# OpenTofu: deploy Falco (runtime security) and Falcosidekick for ELK/Splunk.
# Run: tofu -chdir=infra/opentofu/falco init && tofu -chdir=infra/opentofu/falco plan -var-file=../../desired/falco-main.tfvars.json
# Configure ELK/Splunk via vars or secrets; do not commit tokens.

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

resource "helm_release" "falco" {
  repository       = "https://falcosecurity.github.io/charts"
  chart            = "falco"
  name             = var.falco_release_name
  namespace        = var.falco_namespace
  create_namespace = true
  version          = var.falco_chart_version
  values           = [file(var.falco_values_file)]
  wait             = true
}
