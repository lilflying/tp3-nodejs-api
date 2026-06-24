output "inventory_file" {
  description = "Path to generated Ansible inventory"
  value       = local_file.ansible_inventory.filename
}

output "linux_servers" {
  description = "Linux nodes"
  value       = var.linux_servers
}

output "windows_servers" {
  description = "Windows nodes"
  value       = var.windows_servers
}
