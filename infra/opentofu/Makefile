# One-command local Kind + OpenTofu. Run from repo root: make -C infra/opentofu local-up

LOCAL_ENV     := environments/local
KIND_DIR      := modules/cluster/kind
KIND_CLUSTER_NAME ?= qminiwasm-local
# Use terraform.tfvars if present, else terraform.tfvars.example (no copy required)
TFVARS        := $(if $(wildcard $(LOCAL_ENV)/terraform.tfvars),$(LOCAL_ENV)/terraform.tfvars,$(LOCAL_ENV)/terraform.tfvars.example)
KUBECONFIG_ABS := $(abspath $(LOCAL_ENV)/kubeconfig)

.PHONY: local-up local-down local-plan local-cluster-ensure local-cluster-delete local-check-deps

# One command: ensure cluster, then tofu init + apply (no tfvars copy needed by default)
local-up: local-check-deps local-cluster-ensure
	@echo "Applying OpenTofu in $(LOCAL_ENV) (var-file=$(TFVARS))..."
	cd $(LOCAL_ENV) && KUBECONFIG="$(KUBECONFIG_ABS)" tofu init -upgrade
	cd $(LOCAL_ENV) && KUBECONFIG="$(KUBECONFIG_ABS)" tofu apply -var-file="$(TFVARS)" -auto-approve
	@echo "Done. KUBECONFIG=$(KUBECONFIG_ABS)"

local-plan: local-check-deps local-cluster-ensure
	cd $(LOCAL_ENV) && KUBECONFIG="$(KUBECONFIG_ABS)" tofu init -upgrade
	cd $(LOCAL_ENV) && KUBECONFIG="$(KUBECONFIG_ABS)" tofu plan -var-file="$(TFVARS)"

# Destroy OpenTofu-managed resources, then delete Kind cluster
local-down:
	cd $(LOCAL_ENV) && KUBECONFIG="$(KUBECONFIG_ABS)" tofu destroy -var-file="$(TFVARS)" -auto-approve 2>/dev/null || true
	$(MAKE) -C $(KIND_DIR) kind-delete KIND_CLUSTER_NAME="$(KIND_CLUSTER_NAME)"

# Create Kind cluster if missing; always refresh kubeconfig
local-cluster-ensure:
	@kind get clusters 2>/dev/null | grep -q "$(KIND_CLUSTER_NAME)" || $(MAKE) -C $(KIND_DIR) kind-create KIND_CLUSTER_NAME="$(KIND_CLUSTER_NAME)" KUBECONFIG_OUTPUT="$(abspath $(LOCAL_ENV)/kubeconfig)"
	$(MAKE) -C $(KIND_DIR) kind-kubeconfig KIND_CLUSTER_NAME="$(KIND_CLUSTER_NAME)" KUBECONFIG_OUTPUT="$(abspath $(LOCAL_ENV)/kubeconfig)"

local-cluster-delete:
	$(MAKE) -C $(KIND_DIR) kind-delete KIND_CLUSTER_NAME="$(KIND_CLUSTER_NAME)"

local-check-deps:
	@command -v docker >/dev/null 2>&1 || (echo "Missing: docker. Install Docker and ensure the daemon is running." && exit 1)
	@command -v kind >/dev/null 2>&1 || (echo "Missing: kind. Install from https://kind.sigs.k8s.io/docs/user/quick-start/" && exit 1)
	@command -v tofu >/dev/null 2>&1 || (echo "Missing: tofu. Install from https://opentofu.org/docs/intro/install/" && exit 1)
	@docker info >/dev/null 2>&1 || (echo "Docker daemon not running. Start Docker." && exit 1)
	@echo "Dependencies OK."
