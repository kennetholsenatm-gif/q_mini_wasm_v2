variable "pod_name" {
  type        = string
  description = "Runpod pod name"
  default     = "qminiwasm-training"
}

variable "image_name" {
  type        = string
  description = "Container image (see Runpod / your template)"
  default     = "runpod/pytorch:2.1.0-py3.10-cuda11.8.0-devel"
}

variable "gpu_type_ids" {
  type        = list(string)
  description = "Preferred GPU types (Runpod tries first available)"
  default = [
    "NVIDIA GeForce RTX 4090",
    "NVIDIA GeForce RTX 3090",
    "NVIDIA A40",
  ]
}

variable "data_center_ids" {
  type        = list(string)
  description = "Allowed Runpod data centers"
  default = [
    "US-CA-2",
    "US-TX-3",
  ]
}

variable "gpu_count" {
  type        = number
  description = "Number of GPUs"
  default     = 1
}

variable "cloud_type" {
  type        = string
  description = "COMMUNITY or SECURE (see Runpod docs)"
  default     = "COMMUNITY"
}

variable "support_public_ip" {
  type        = bool
  default     = true
}

variable "volume_in_gb" {
  type        = number
  description = "Network volume size (GB)"
  default     = 20
}

variable "container_disk_in_gb" {
  type        = number
  description = "Container disk (GB)"
  default     = 20
}

variable "ports" {
  type        = list(string)
  description = "Exposed ports, e.g. 8888/http, 22/tcp"
  default     = ["8888/http", "22/tcp"]
}

variable "env" {
  type        = map(string)
  description = <<-EOT
    Container environment variables. Defaults set ACCELERATOR=cuda so `python -m engine`
    uses the Runpod GPU once PyTorch sees CUDA inside the container. Override in terraform.tfvars if needed.
  EOT
  default = {
    ACCELERATOR              = "cuda"
    NVIDIA_VISIBLE_DEVICES   = "all"
  }
}
