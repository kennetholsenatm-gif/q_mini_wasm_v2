# OpenTofu Configuration for Local Autonomous Solace Agent Mesh
# This configuration sets up the local cloud infrastructure using OpenNebula

terraform {
  required_version = ">= 1.0"
  
  required_providers {
    docker = {
      source  = "kreuzwerker/docker"
      version = "~> 3.0"
    }
    local = {
      source  = "hashicorp/local"
      version = "~> 2.0"
    }
    tls = {
      source  = "hashicorp/tls"
      version = "~> 4.0"
    }
  }

  # Local backend configuration
  backend "local" {
    path = "terraform.tfstate"
  }
}

# Configure Docker provider
provider "docker" {
  host = "unix:///var/run/docker.sock"
}

# Generate ML-KEM Post-Quantum Cryptography keys for PQC readiness
resource "tls_private_key" "ml_kem_key" {
  algorithm = "ECDSA"
  ecdsa_curve = "P384"
}

# Generate self-signed certificates for mTLS
resource "tls_self_signed_cert" "mesh_cert" {
  private_key_pem = tls_private_key.ml_kem_key.private_key_pem

  subject {
    common_name  = "autonomous-agent-mesh.local"
    organization = "Autonomous Solace Agent Mesh"
  }

  validity_period_hours = 8760

  allowed_uses = [
    "key_encipherment",
    "digital_signature",
    "server_auth",
    "client_auth"
  ]
}

# Create Docker network for the mesh
resource "docker_network" "mesh_net" {
  name = "mesh_net"
  driver = "bridge"
  
  ipam_config {
    subnet = "100.64.0.0/16"
    gateway = "100.64.0.1"
  }
  
  labels {
    label = "environment"
    value = "local"
  }
  
  labels {
    label = "purpose"
    value = "autonomous-agent-mesh"
  }
}

# Create PostgreSQL database for OpenNebula and applications
resource "docker_container" "postgres_db" {
  name  = "opennebula-db"
  image = "postgres:15"
  
  env = [
    "POSTGRES_DB=one",
    "POSTGRES_USER=oneadmin",
    "POSTGRES_PASSWORD=oneadmin",
    "POSTGRES_INITDB_ARGS=--auth-host=scram-sha-256"
  ]
  
  networks_advanced {
    name = docker_network.mesh_net.name
  }
  
  mounts {
    type = "volume"
    target = "/var/lib/postgresql/data"
    source = "opennebula_db"
  }
  
  restart = "unless-stopped"
}

# Create OpenNebula Frontend
resource "docker_container" "opennebula_frontend" {
  name  = "opennebula-frontend"
  image = "opennebula/one:6.10.0"
  
  env = [
    "ONE_XMLRPC=http://opennebula-frontend:2633/RPC2",
    "ONE_AUTH=/var/lib/one/.one/one_auth",
    "ONEGATE_ENDPOINT=http://opennebula-frontend:5030",
    "ONEFLOW_ENDPOINT=http://opennebula-frontend:2474"
  ]
  
  networks_advanced {
    name = docker_network.mesh_net.name
  }
  
  ports {
    internal = 9869
    external = 9869
  }
  
  ports {
    internal = 2633
    external = 2633
  }
  
  mounts {
    type = "volume"
    target = "/var/lib/one"
    source = "opennebula_data"
  }
  
  mounts {
    type = "volume"
    target = "/var/log/one"
    source = "opennebula_logs"
  }
  
  mounts {
    type = "volume"
    target = "/var/lib/one/.ssh"
    source = "opennebula_ssh"
  }
  
  command = [
    "bash", "-c", 
    "sleep 10 && /usr/bin/oned & sleep 5 && /usr/bin/oneadmin & sleep 5 && /usr/bin/oneflow-server & sleep 5 && /usr/bin/onegate-server & sleep 5 && /usr/bin/sunstone-server & tail -f /var/log/one/oned.log"
  ]
  
  restart = "unless-stopped"
  privileged = true
}

# Create Solace Message VPN
resource "docker_container" "solace_message_vpn" {
  name  = "solace-message-vpn"
  image = "solace/solace-pubsub-standard:latest"
  
  env = [
    "SOLACE_ADMIN_PASSWORD=admin",
    "SOLACE_MESSAGE_VPN=autonomous_agent_mesh"
  ]
  
  networks_advanced {
    name = docker_network.mesh_net.name
  }
  
  ports {
    internal = 8080
    external = 8080
  }
  
  ports {
    internal = 55555
    external = 55555
  }
  
  ports {
    internal = 8443
    external = 8443
  }
  
  ports {
    internal = 22
    external = 2222
  }
  
  mounts {
    type = "volume"
    target = "/var/lib/solace"
    source = "solace_data"
  }
  
  mounts {
    type = "volume"
    target = "/var/log/solace"
    source = "solace_logs"
  }
  
  restart = "unless-stopped"
  privileged = true
}

# Create Gitea Server
resource "docker_container" "gitea_server" {
  name  = "gitea-server"
  image = "gitea/gitea:1.21.4"
  
  env = [
    "USER_UID=1000",
    "USER_GID=1000",
    "GITEA__database__DB_TYPE=postgres",
    "GITEA__database__HOST=opennebula-db:5432",
    "GITEA__database__NAME=gitea",
    "GITEA__database__USER=gitea",
    "GITEA__database__PASSWD=gitea",
    "GITEA__security__INSTALL_LOCK=true",
    "GITEA__security__SECRET_KEY=${random_password.gitea_secret.result}",
    "GITEA__security__INTERNAL_TOKEN=${random_password.gitea_internal.result}"
  ]
  
  networks_advanced {
    name = docker_network.mesh_net.name
  }
  
  ports {
    internal = 3000
    external = 3000
  }
  
  ports {
    internal = 22
    external = 2223
  }
  
  mounts {
    type = "volume"
    target = "/data"
    source = "gitea_data"
  }
  
  mounts {
    type = "volume"
    target = "/var/log/gitea"
    source = "gitea_logs"
  }
  
  restart = "unless-stopped"
}

# Create n8n Workflow Orchestrator
resource "docker_container" "n8n_orchestrator" {
  name  = "n8n-orchestrator"
  image = "n8nio/n8n:latest"
  
  env = [
    "N8N_HOST=n8n-orchestrator",
    "N8N_PORT=5678",
    "N8N_PROTOCOL=https",
    "N8N_SSL_KEY=/data/n8n.key",
    "N8N_SSL_CERT=/data/n8n.crt",
    "DB_TYPE=postgres",
    "DB_POSTGRES_HOST=opennebula-db",
    "DB_POSTGRES_PORT=5432",
    "DB_POSTGRES_DATABASE=n8n",
    "DB_POSTGRES_USER=n8n",
    "DB_POSTGRES_PASSWORD=n8n",
    "N8N_ENCRYPTION_KEY=${random_password.n8n_encryption.result}",
    "WEBHOOK_URL=https://n8n-orchestrator:5678/webhook/"
  ]
  
  networks_advanced {
    name = docker_network.mesh_net.name
  }
  
  ports {
    internal = 5678
    external = 5678
  }
  
  mounts {
    type = "volume"
    target = "/home/node/.n8n"
    source = "n8n_data"
  }
  
  restart = "unless-stopped"
}

# Create Vault for Secrets Management
resource "docker_container" "vault_server" {
  name  = "vault-server"
  image = "vault:1.15.0"
  
  env = [
    "VAULT_DEV_LISTEN_ADDRESS=0.0.0.0:8200",
    "VAULT_DEV_ROOT_TOKEN_ID=${random_password.vault_root.result}"
  ]
  
  networks_advanced {
    name = docker_network.mesh_net.name
  }
  
  ports {
    internal = 8200
    external = 8200
  }
  
  mounts {
    type = "volume"
    target = "/vault/data"
    source = "vault_data"
  }
  
  mounts {
    type = "volume"
    target = "/vault/logs"
    source = "vault_logs"
  }
  
  restart = "unless-stopped"
  capabilities {
    add = ["IPC_LOCK"]
  }
}

# Generate random passwords for security
resource "random_password" "gitea_secret" {
  length = 32
  special = true
}

resource "random_password" "gitea_internal" {
  length = 64
  special = true
}

resource "random_password" "n8n_encryption" {
  length = 32
  special = true
}

resource "random_password" "vault_root" {
  length = 32
  special = true
}

# Create local files for configuration
resource "local_file" "solace_config" {
  filename = "${path.module}/solace_config.json"
  content = jsonencode({
    message_vpn = "autonomous_agent_mesh"
    authentication = {
      type = "mTLS"
      client_certificates = true
      basic_auth = false
    }
    acl_profile = {
      name = "strict_a2a_profile"
      default_action = "disallow"
      exceptions = [
        {
          topic = "a2a/v1/>"
          action = "publish"
        }
      ]
    }
  })
}

resource "local_file" "gitea_config" {
  filename = "${path.module}/gitea_config.ini"
  content = <<-EOF
    [database]
    DB_TYPE = postgres
    HOST = opennebula-db:5432
    NAME = gitea
    USER = gitea
    PASSWD = gitea
    
    [security]
    INSTALL_LOCK = true
    SECRET_KEY = ${random_password.gitea_secret.result}
    INTERNAL_TOKEN = ${random_password.gitea_internal.result}
    
    [server]
    DOMAIN = gitea-server
    HTTP_PORT = 3000
    ROOT_URL = http://gitea-server:3000/
    
    [webhook]
    ALLOWED_HOST_LIST = n8n-orchestrator
  EOF
}

# Output configuration information
output "mesh_network_info" {
  value = {
    network_name = docker_network.mesh_net.name
    subnet = "100.64.0.0/16"
    gateway = "100.64.0.1"
  }
}

output "services_info" {
  value = {
    opennebula_frontend = {
      url = "http://opennebula-frontend:9869"
      api = "http://opennebula-frontend:2633"
    }
    solace_manager = {
      url = "http://solace-message-vpn:8080"
      smf_port = 55555
      smf_ssl_port = 8443
    }
    gitea_server = {
      url = "http://gitea-server:3000"
      ssh_port = 2223
    }
    n8n_orchestrator = {
      url = "https://n8n-orchestrator:5678"
      webhook_url = "https://n8n-orchestrator:5678/webhook/"
    }
    vault_server = {
      url = "http://vault-server:8200"
      root_token = random_password.vault_root.result
    }
  }
}

output "pqc_keys" {
  value = {
    ml_kem_private_key = tls_private_key.ml_kem_key.private_key_pem
    mesh_certificate = tls_self_signed_cert.mesh_cert.cert_pem
  }
  sensitive = true
}