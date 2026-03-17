# FreeIPA Server Module for Hierarchical Edge-Quantum AI Architecture
# Provides centralized LDAP, Kerberos, and PKI services for zero-trust environments

terraform {
  required_version = ">= 1.0"
  required_providers {
    docker = {
      source  = "kreuzwerker/docker"
      version = "~> 3.0"
    }
    kubernetes = {
      source  = "hashicorp/kubernetes"
      version = "~> 2.0"
    }
  }
}

# FreeIPA Server Container
resource "docker_container" "freeipa_server" {
  name    = var.freeipa_server_name
  image   = var.freeipa_image
  restart = "unless-stopped"

  # Network configuration
  networks_advanced = [
    {
      name = var.freeipa_network_name
    }
  ]

  # Port mappings
  ports = [
    {
      internal = 80
      external = var.freeipa_http_port
    },
    {
      internal = 443
      external = var.freeipa_https_port
    },
    {
      internal = 389
      external = var.freeipa_ldap_port
    },
    {
      internal = 636
      external = var.freeipa_ldaps_port
    },
    {
      internal = 88
      external = var.freeipa_kdc_port
    },
    {
      internal = 464
      external = var.freeipa_kpasswd_port
    },
    {
      internal = 53
      external = var.freeipa_dns_port
    },
    {
      internal = 123
      external = var.freeipa_ntp_port
    }
  ]

  # Environment variables
  env = [
    "IPA_SERVER_HOSTNAME=${var.freeipa_server_hostname}",
    "IPA_DOMAIN=${var.freeipa_domain}",
    "IPA_REALM=${var.freeipa_realm}",
    "IPA_ADMIN_PASSWORD=${var.freeipa_admin_password}",
    "IPA_DS_PASSWORD=${var.freeipa_ds_password}",
    "IPA_CA_SUBJECT=${var.freeipa_ca_subject}",
    "IPA_DNS_FORWARDERS=${join(",", var.freeipa_dns_forwarders)}",
    "IPA_NTP_SERVERS=${join(",", var.freeipa_ntp_servers)}",
    "IPA_ENABLE_DNS_UPDATES=true",
    "IPA_ENABLE_SSH_KEYS=true",
    "IPA_ENABLE_SSH_HOSTKEYS=true",
    "IPA_SERVER_INSTALL_OPTS=--no-ntp --no-dns-ssh-keygen --no-forwarders --no-reverse"
  ]

  # Volumes
  mounts {
    type   = "volume"
    source = docker_volume.freeipa_data.name
    target = "/data"
  }

  mounts {
    type   = "volume"
    source = docker_volume.freeipa_cache.name
    target = "/run"
  }

  mounts {
    type   = "volume"
    source = docker_volume.freeipa_sssd.name
    target = "/var/lib/sss"
  }

  mounts {
    type     = "bind"
    source   = "/sys/fs/cgroup"
    target   = "/sys/fs/cgroup"
    read_only = true
  }

  # Required capabilities
  cap_add = [
    "SYS_TIME",
    "SYS_NICE",
    "SYS_RESOURCE",
    "NET_BIND_SERVICE",
    "CHOWN",
    "SETUID",
    "SETGID",
    "DAC_READ_SEARCH",
    "FOWNER",
    "SETPCAP"
  ]

  # Privileged mode required for FreeIPA
  privileged = true

  # DNS configuration
  dns = var.freeipa_dns_servers

  # Health check
  healthcheck {
    test     = ["CMD", "ipa", "healthcheck", "--all"]
    interval = "30s"
    timeout  = "10s"
    retries  = 5
    start_period = "120s"
  }

  # Resource limits
  resources {
    memory = var.freeipa_memory_limit
    cpu    = var.freeipa_cpu_limit
  }

  # Dependencies
  depends_on = [
    docker_network.freeipa_network,
    docker_volume.freeipa_data,
    docker_volume.freeipa_cache,
    docker_volume.freeipa_sssd
  ]
}

# FreeIPA Client Container
resource "docker_container" "freeipa_client" {
  name    = var.freeipa_client_name
  image   = var.freeipa_client_image
  restart = "unless-stopped"

  # Network configuration
  networks_advanced = [
    {
      name = var.freeipa_network_name
    }
  ]

  # Environment variables
  env = [
    "IPA_SERVER=${var.freeipa_server_hostname}",
    "IPA_DOMAIN=${var.freeipa_domain}",
    "IPA_REALM=${var.freeipa_realm}",
    "IPA_ADMIN_PASSWORD=${var.freeipa_admin_password}",
    "IPA_CLIENT_INSTALL_OPTS=--no-ntp --no-ssh --no-sshfp --no-sudo"
  ]

  # Volumes
  mounts {
    type   = "volume"
    source = docker_volume.freeipa_client_data.name
    target = "/etc/ipa"
  }

  mounts {
    type   = "volume"
    source = docker_volume.freeipa_client_cache.name
    target = "/var/lib/sss"
  }

  # Required capabilities
  cap_add = [
    "SYS_TIME",
    "SYS_NICE",
    "SYS_RESOURCE",
    "NET_BIND_SERVICE",
    "CHOWN",
    "SETUID",
    "SETGID",
    "DAC_READ_SEARCH",
    "FOWNER",
    "SETPCAP"
  ]

  # Privileged mode required for FreeIPA client
  privileged = true

  # Resource limits
  resources {
    memory = var.freeipa_client_memory_limit
    cpu    = var.freeipa_client_cpu_limit
  }

  # Dependencies
  depends_on = [
    docker_container.freeipa_server,
    docker_volume.freeipa_client_data,
    docker_volume.freeipa_client_cache
  ]
}

# FreeIPA Network
resource "docker_network" "freeipa_network" {
  name       = var.freeipa_network_name
  driver     = "bridge"
  attachable = true

  ipam {
    config {
      subnet  = var.freeipa_network_subnet
      gateway = var.freeipa_network_gateway
    }
  }
}

# FreeIPA Data Volume
resource "docker_volume" "freeipa_data" {
  name = var.freeipa_data_volume_name
  driver = "local"
}

# FreeIPA Cache Volume
resource "docker_volume" "freeipa_cache" {
  name = var.freeipa_cache_volume_name
  driver = "local"
}

# FreeIPA SSSD Volume
resource "docker_volume" "freeipa_sssd" {
  name = var.freeipa_sssd_volume_name
  driver = "local"
}

# FreeIPA Client Data Volume
resource "docker_volume" "freeipa_client_data" {
  name = var.freeipa_client_data_volume_name
  driver = "local"
}

# FreeIPA Client Cache Volume
resource "docker_volume" "freeipa_client_cache" {
  name = var.freeipa_client_cache_volume_name
  driver = "local"
}

# Kubernetes Deployment (Alternative to Docker Compose)
resource "kubernetes_deployment" "freeipa_server" {
  count = var.enable_kubernetes ? 1 : 0

  metadata {
    name      = var.freeipa_server_name
    namespace = var.kubernetes_namespace
    labels = {
      app     = "freeipa-server"
      version = var.freeipa_version
    }
  }

  spec {
    replicas = 1

    selector {
      match_labels = {
        app = "freeipa-server"
      }
    }

    template {
      metadata {
        labels = {
          app = "freeipa-server"
        }
      }

      spec {
        service_account_name = var.kubernetes_service_account
        security_context {
          run_as_user  = 0
          run_as_group = 0
        }

        container {
          name  = "freeipa-server"
          image = var.freeipa_image

          # Environment variables
          env = [
            {
              name  = "IPA_SERVER_HOSTNAME"
              value = var.freeipa_server_hostname
            },
            {
              name  = "IPA_DOMAIN"
              value = var.freeipa_domain
            },
            {
              name  = "IPA_REALM"
              value = var.freeipa_realm
            },
            {
              name  = "IPA_ADMIN_PASSWORD"
              value = var.freeipa_admin_password
            },
            {
              name  = "IPA_DS_PASSWORD"
              value = var.freeipa_ds_password
            }
          ]

          # Ports
          port {
            container_port = 80
            protocol       = "TCP"
          }

          port {
            container_port = 443
            protocol       = "TCP"
          }

          port {
            container_port = 389
            protocol       = "TCP"
          }

          port {
            container_port = 636
            protocol       = "TCP"
          }

          port {
            container_port = 88
            protocol       = "TCP"
          }

          port {
            container_port = 464
            protocol       = "TCP"
          }

          port {
            container_port = 53
            protocol       = "TCP"
          }

          port {
            container_port = 123
            protocol       = "UDP"
          }

          # Volume mounts
          volume_mount {
            name       = "freeipa-data"
            mount_path = "/data"
          }

          volume_mount {
            name       = "freeipa-cache"
            mount_path = "/run"
          }

          volume_mount {
            name       = "freeipa-sssd"
            mount_path = "/var/lib/sss"
          }

          # Resource limits
          resources {
            limits = {
              memory = var.freeipa_memory_limit
              cpu    = var.freeipa_cpu_limit
            }

            requests = {
              memory = var.freeipa_memory_request
              cpu    = var.freeipa_cpu_request
            }
          }

          # Health check
          liveness_probe {
            http_get {
              path = "/health"
              port = 80
            }
            initial_delay_seconds = 120
            period_seconds        = 30
            timeout_seconds       = 10
            failure_threshold     = 5
          }

          readiness_probe {
            http_get {
              path = "/health"
              port = 80
            }
            initial_delay_seconds = 30
            period_seconds        = 10
            timeout_seconds       = 5
            failure_threshold     = 3
          }
        }

        # Volumes
        volume {
          name = "freeipa-data"
          persistent_volume_claim {
            claim_name = var.freeipa_data_pvc_name
          }
        }

        volume {
          name = "freeipa-cache"
          persistent_volume_claim {
            claim_name = var.freeipa_cache_pvc_name
          }
        }

        volume {
          name = "freeipa-sssd"
          persistent_volume_claim {
            claim_name = var.freeipa_sssd_pvc_name
          }
        }
      }
    }
  }
}

# Kubernetes Service
resource "kubernetes_service" "freeipa_server" {
  count = var.enable_kubernetes ? 1 : 0

  metadata {
    name      = var.freeipa_server_name
    namespace = var.kubernetes_namespace
    labels = {
      app     = "freeipa-server"
      version = var.freeipa_version
    }
  }

  spec {
    selector = {
      app = "freeipa-server"
    }

    port {
      port        = 80
      target_port = 80
      name        = "http"
    }

    port {
      port        = 443
      target_port = 443
      name        = "https"
    }

    port {
      port        = 389
      target_port = 389
      name        = "ldap"
    }

    port {
      port        = 636
      target_port = 636
      name        = "ldaps"
    }

    port {
      port        = 88
      target_port = 88
      name        = "kerberos"
    }

    port {
      port        = 464
      target_port = 464
      name        = "kpasswd"
    }

    port {
      port        = 53
      target_port = 53
      name        = "dns"
    }

    port {
      port        = 123
      target_port = 123
      name        = "ntp"
      protocol    = "UDP"
    }

    type = "LoadBalancer"
  }
}