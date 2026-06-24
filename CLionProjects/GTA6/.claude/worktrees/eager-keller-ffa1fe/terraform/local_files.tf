resource "local_file" "ansible_inventory" {
  filename = var.inventory_path
  content = templatefile("${path.module}/inventory.tpl", {
    linux_servers    = var.linux_servers
    windows_servers  = var.windows_servers
    windows_user     = var.windows_user
    windows_password = var.windows_password
  })
}
