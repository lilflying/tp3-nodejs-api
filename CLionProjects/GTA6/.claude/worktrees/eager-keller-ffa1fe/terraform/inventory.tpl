all:
  hosts:
    localhost:
      ansible_connection: local
      ansible_host: 127.0.0.1
      ansible_python_interpreter: /usr/bin/python3

  children:
    linux:
      hosts:
%{ for server in linux_servers ~}
        ${server.name}:
          ansible_host: ${server.ip}
          ansible_user: ${server.user}
          ansible_ssh_common_args: '-o StrictHostKeyChecking=no'
          os_family: ${server.os}
%{ endfor ~}

    windows:
      hosts:
%{ for server in windows_servers ~}
        ${server.name}:
          ansible_host: ${server.ip}
          ansible_user: ${windows_user}
          ansible_password: ${windows_password}
          ansible_connection: winrm
          ansible_winrm_transport: basic
          ansible_winrm_server_cert_validation: ignore
          ansible_port: 5985
%{ endfor ~}
