variable "inventory_path" {
  description = "Path to Ansible inventory file"
  type        = string
  default     = "../ansible_inputs/inventory.yaml"
}

variable "linux_servers" {
  description = "Linux target nodes"
  type = list(object({
    name = string
    ip   = string
    os   = string
    user = string
  }))
  default = [
    { name = "ubuntu-node", ip = "192.168.64.3", os = "ubuntu", user = "debian" },
    { name = "fedora-node", ip = "192.168.64.4", os = "fedora", user = "admin"  },
    { name = "redhat-node", ip = "192.168.64.5", os = "redhat", user = "admin"  },
  ]
}

variable "windows_servers" {
  description = "Windows target nodes (managed via WinRM)"
  type = list(object({
    name = string
    ip   = string
  }))
  default = [
    { name = "windows-node", ip = "192.168.64.6" },
  ]
}

variable "windows_user" {
  default = "Martin"
}

variable "windows_password" {
  default   = "AZ12az12"
  sensitive = true
}
