variable "region" {
  default = "eu-west-3" # Paris
}

variable "ami_id" {
  description = "AMI Ubuntu 22.04 LTS (dépend de la région)"
  default     = "ami-0c1319b99d0ba39e9"
}

variable "instance_type" {
  default = "t3.small"
}

variable "key_name" {
  description = "Nom de la paire de clés SSH AWS"
  default     = "devops-tp-key"
}
