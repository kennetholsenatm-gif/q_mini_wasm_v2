# DockerOS Platform Standard: AlmaLinux, Foreman, and Foreman Smart Proxy

This document defines the standard FOSS (Free and Open Source Software) platform for running and managing containerized workloads in this project. We standardize on the closest FOSS equivalents to Red Hat Satellite and Red Hat Satellite Capsule.

## Standard Components

| Red Hat product      | FOSS equivalent        | Role |
|----------------------|------------------------|------|
| RHEL                 | **AlmaLinux** (9; 10 when available) | Base OS for hosts and optional container images |
| Red Hat Satellite    | **Foreman** (with Katello for content) | Lifecycle, content, provisioning, configuration |
| Red Hat Satellite Capsule | **Foreman Smart Proxy** | Distributed services: TFTP, DNS, DHCP, discovery, remote execution |

## AlmaLinux

- **Current standard:** **AlmaLinux 9** (1:1 binary compatibility with RHEL 9).
- **Future:** **AlmaLinux 10** when released (track RHEL 10); use for new deployments when available.
- **Use cases:**
  - **Host OS** for machines that run Docker, Kubernetes, or other runtime nodes (the “DockerOS” hosts).
  - **Container base images** (optional): use `almalinux:9` or `almalinux:9-minimal` for application images that require an RHEL-like OS inside the container.
- **References:**
  - [AlmaLinux](https://almalinux.org/)
  - [AlmaLinux 9](https://wiki.almalinux.org/release-notes/AlmaLinux-9.html)

## Foreman (Satellite equivalent)

- **Role:** Central lifecycle, content, and provisioning for hosts (including Docker/Kubernetes nodes).
- **Capabilities:** Host registration, content (repos, errata), provisioning (PXE, discovery), configuration (Puppet/Ansible), reporting.
- **Deployment:** Run Foreman on AlmaLinux 9 (or 10 when available). For content and subscription-like workflows, use **Katello** (Foreman plugin).
- **References:**
  - [Foreman](https://theforeman.org/)
  - [Installing Foreman on Enterprise Linux](https://docs.theforeman.org/nightly/Installing_Server/index-foreman-el.html)

## Foreman Smart Proxy (Capsule equivalent)

- **Role:** Remote service node that extends Foreman to the network (TFTP, DNS, DHCP, discovery, remote execution, optional content cache).
- **Deployment:** Install Smart Proxy on AlmaLinux 9 (or 10) on dedicated or shared nodes. Version must match the Foreman server. Use a clean system (no conflicting services/users).
- **References:**
  - [Installing a Smart Proxy on Enterprise Linux](https://docs.theforeman.org/nightly/Installing_Proxy/index-foreman-el.html)
  - [Katello Smart Proxy](https://theforeman.org/plugins/katello/nightly/installation/smart_proxy.html) (when using Katello for content)

## DockerOS Host Standard

- **Base OS:** AlmaLinux 9 (or AlmaLinux 10 when available) on every host that runs Docker (and optionally Kubernetes).
- **Management:** Register hosts with Foreman; use Smart Proxies where needed for PXE, DNS, DHCP, discovery, or remote execution.
- **Containers:** Prefer AlmaLinux-based container images where an RHEL-like OS inside the container is required (see repo `containers/` and `hardening_manifest.yaml`). Other images (e.g. `python:3.11-slim`, `nginx:alpine`) may still be used for convenience; the *host* OS standard remains AlmaLinux + Foreman/Smart Proxy.

## Container Base Images in This Repo

- **FOSS / non–Iron Bank:** Standard base is **AlmaLinux 9** (e.g. `almalinux:9-minimal` or `almalinux:9`) for images that must align with this DockerOS standard.
- **Iron Bank / DoD:** When an approved AlmaLinux (or UBI) base is available in your registry, use that image in `containers/*/hardening_manifest.yaml` and in Dockerfiles. The platform standard (AlmaLinux on hosts, Foreman, Smart Proxy) is unchanged.

## Summary

- **Hosts:** AlmaLinux 9 (or 10 when available), managed by Foreman, with Foreman Smart Proxy where Capsule-like behavior is needed.
- **Containers:** AlmaLinux 9–based images for RHEL-like container OS; hardening and Iron Bank manifests reference this standard.
