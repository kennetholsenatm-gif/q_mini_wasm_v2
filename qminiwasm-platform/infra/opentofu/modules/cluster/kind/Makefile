# Kind cluster for local K8s dev. Run from repo root or this dir.
# Requires: Docker, Kind (https://kind.sigs.k8s.io/docs/user/quick-start/)

KIND_CLUSTER_NAME ?= qminiwasm-local
KIND_CONFIG ?= kind-config.yaml
# Where to copy kubeconfig for environments/local (relative to this Makefile)
KUBECONFIG_OUTPUT ?= ../../environments/local/kubeconfig

.PHONY: kind-create kind-delete kind-kubeconfig kind-status

kind-create:
	kind create cluster --name "$(KIND_CLUSTER_NAME)" --config "$(KIND_CONFIG)"
	$(MAKE) kind-kubeconfig

kind-delete:
	kind delete cluster --name "$(KIND_CLUSTER_NAME)"

# Copy kubeconfig to environments/local so OpenTofu can use it (or set KUBECONFIG to kind get kubeconfig path)
kind-kubeconfig:
	@mkdir -p "$$(dirname "$(KUBECONFIG_OUTPUT)")"
	kind get kubeconfig --name "$(KIND_CLUSTER_NAME)" > "$(KUBECONFIG_OUTPUT)"
	@echo "Kubeconfig written to $(KUBECONFIG_OUTPUT). Export: export KUBECONFIG=$$(cd $$(dirname $(KUBECONFIG_OUTPUT)) && pwd)/kubeconfig"

kind-status:
	kind get clusters
	kubectl cluster-info --context kind-$(KIND_CLUSTER_NAME) 2>/dev/null || true
