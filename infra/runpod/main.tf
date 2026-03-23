provider "runpod" {
  # Authenticates via RUNPOD_API_KEY (set by tofu.sh from RUNPOD_TOKEN if needed).
}

resource "runpod_pod" "training" {
  name                 = var.pod_name
  image_name           = var.image_name
  gpu_type_ids         = var.gpu_type_ids
  data_center_ids      = var.data_center_ids
  gpu_count            = var.gpu_count
  cloud_type           = var.cloud_type
  support_public_ip    = var.support_public_ip
  volume_in_gb         = var.volume_in_gb
  container_disk_in_gb = var.container_disk_in_gb
  ports                = var.ports
  env                  = var.env
}
