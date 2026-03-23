output "pod_id" {
  description = "Runpod pod id"
  value       = runpod_pod.training.id
}

output "public_ip" {
  description = "Public IP when support_public_ip is true"
  value       = runpod_pod.training.public_ip
}

output "cost_per_hr" {
  description = "Reported cost per hour (provider-dependent)"
  value       = runpod_pod.training.cost_per_hr
}

output "desired_status" {
  value = runpod_pod.training.desired_status
}
