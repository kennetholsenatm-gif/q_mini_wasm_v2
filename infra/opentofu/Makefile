# One-command local Kind + OpenTofu. Run from repo root: make -C infra/opentofu local-up

LOCAL_ENV     := environments/local
KIND_DIR      := modules/cluster/kind
KIND_CLUSTER_NAME ?= qminiwasm-local
# Use terraform.tfvars if present, else terraform.tfvars.example (no copy required)
TFVARS        := $(if $(wildcard $(LOCAL_ENV)/terraform.tfvars),$(LOCAL_ENV)/terraform.tfvars,$(LOCAL_ENV)/terraform.tfvars.example)
# Filename only: used with -var-file after cd into LOCAL_ENV so path resolves correctly
TFVARS_FILE   := $(if $(wildcard $(LOCAL_ENV)/terraform.tfvars),terraform.tfvars,terraform.tfvars.example)
KUBECONFIG_ABS := $(abspath $(LOCAL_ENV)/kubeconfig)

.PHONY: local-up local-down local-plan local-verify local-cluster-ensure local-cluster-delete local-check-deps

# One command: ensure cluster, then tofu init + apply (no tfvars copy needed by default)
local-up: local-check-deps local-cluster-ensure
	@echo "Applying OpenTofu in $(LOCAL_ENV) (var-file=$(TFVARS_FILE))..."
	cd $(LOCAL_ENV) && KUBECONFIG="$(KUBECONFIG_ABS)" tofu init -upgrade
	cd $(LOCAL_ENV) && KUBECONFIG="$(KUBECONFIG_ABS)" tofu apply -var-file="$(TFVARS_FILE)" -auto-approve
	@echo "Done. KUBECONFIG=$(KUBECONFIG_ABS)"

local-plan: local-check-deps local-cluster-ensure
	cd $(LOCAL_ENV) && KUBECONFIG="$(KUBECONFIG_ABS)" tofu init -upgrade
	cd $(LOCAL_ENV) && KUBECONFIG="$(KUBECONFIG_ABS)" tofu plan -var-file="$(TFVARS_FILE)"

# Destroy OpenTofu-managed resources, then delete Kind cluster
local-down:
	cd $(LOCAL_ENV) && KUBECONFIG="$(KUBECONFIG_ABS)" tofu destroy -var-file="$(TFVARS_FILE)" -auto-approve 2>/dev/null || true
	$(MAKE) -C $(KIND_DIR) kind-delete KIND_CLUSTER_NAME="$(KIND_CLUSTER_NAME)"

# Verify cluster and Teleport pods (run after local-up)
local-verify:
	@KUBECONFIG="$(KUBECONFIG_ABS)" kubectl get pods -n teleport 2>/dev/null || (echo "Run 'make -C infra/opentofu local-up' first and set KUBECONFIG." && exit 1)
	@KUBECONFIG="$(KUBECONFIG_ABS)" kubectl get pods -n teleport --no-headers 2>/dev/null | awk '{print $$3}' | grep -qv Running && echo "If teleport-auth is not Running, see infra/teleport/README.md" || true

# Create Kind cluster if missing; always refresh kubeconfig (uses bash script for Windows/Git Bash compatibility)
# Use relative path so bash gets scripts/ensure-kind-cluster.sh (avoids Windows CURDIR backslashes breaking the path)
local-cluster-ensure:
	@bash scripts/ensure-kind-cluster.sh "$(KIND_CLUSTER_NAME)" "$(KUBECONFIG_ABS)" "$(KIND_DIR)"

local-cluster-delete:
	$(MAKE) -C $(KIND_DIR) kind-delete KIND_CLUSTER_NAME="$(KIND_CLUSTER_NAME)"

# Portable dep check (Unix and Windows): run tool and check exit code (no /dev/null for Windows)
local-check-deps:
	@docker --version || (echo "Missing: docker. Install Docker and ensure the daemon is running." && exit 1)
	@kind --version || (echo "Missing: kind. Install from https://kind.sigs.k8s.io/docs/user/quick-start/" && exit 1)
	@tofu version || (echo "Missing: tofu. Install from https://opentofu.org/docs/intro/install/" && exit 1)
	@docker info || (echo "Docker daemon not running. Start Docker." && exit 1)
	@echo "Dependencies OK."
