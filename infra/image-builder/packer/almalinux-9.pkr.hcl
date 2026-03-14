# Packer 1.7+ HCL: AlmaLinux 9 Golden Image (QEMU/QCOW2)
# DockerOS standard: docs/DockerOS-Platform-Standard.md
# Build from this directory: packer init . && packer validate . && packer build .

packer {
  required_plugins {
    qemu = {
      source  = "github.com/hashicorp/qemu"
      version = "~> 1.0"
    }
  }
}

variable "iso_url" {
  type        = string
  default     = "https://repo.almalinux.org/almalinux/9/isos/x86_64/AlmaLinux-9-latest-x86_64-minimal.iso"
  description = "URL of the AlmaLinux 9 minimal ISO"
}

variable "iso_checksum" {
  type        = string
  default     = "sha256:6624593b53c89195f7b68b2070a280d47b4276a7cbc10d2216661bf35d4f442b"
  description = "SHA256 checksum of the ISO (update when ISO changes)"
}

variable "ssh_password" {
  type        = string
  default     = "packer"
  sensitive   = true
  description = "Root password set by kickstart and used by Packer SSH (use PKR_VAR_ssh_password or -var-file)"
}

variable "vm_name" {
  type        = string
  default     = "almalinux9-golden"
  description = "Name of the output QCOW2 image file"
}

variable "headless" {
  type        = bool
  default     = false
  description = "Run QEMU without GUI (set true for CI)"
}

source "qemu" "almalinux9" {
  iso_url          = var.iso_url
  iso_checksum     = var.iso_checksum
  output_directory = "output-almalinux9"
  vm_name          = var.vm_name
  format           = "qcow2"
  disk_size        = "20G"
  memory           = 2048
  disk_interface   = "virtio"
  net_device      = "virtio-net"
  accelerator      = "kvm"
  headless         = var.headless
  http_directory   = "http"
  boot_wait        = "5s"
  boot_command = [
    "<tab><wait>",
    " inst.ks=http://{{ .HTTPIP }}:{{ .HTTPPort }}/ks.cfg<enter><wait>"
  ]
  ssh_username  = "root"
  ssh_password  = var.ssh_password
  ssh_timeout   = "20m"
  shutdown_command = "echo '${var.ssh_password}' | sudo -S shutdown -P now"
  # RHEL 9 / AlmaLinux 9 hosts: QEMU may not support qemu64; set cpu_model = "host" if you see CPU-related boot failures
  # cpu_model   = "host"
}

build {
  sources = ["source.qemu.almalinux9"]

  provisioner "shell" {
    script = "scripts/provision.sh"
  }
}
