variable "aws_region" {
  description = "AWS region for resources"
  type        = string
  default     = "us-east-1"
}

variable "project_name" {
  description = "Project name used for resource naming"
  type        = string
  default     = "anofox-benchmark"
}

variable "create_s3_bucket" {
  description = "Whether to create an S3 bucket for benchmark results"
  type        = bool
  default     = false
}

variable "s3_bucket_name" {
  description = "Name of the S3 bucket for results (only used if create_s3_bucket is true)"
  type        = string
  default     = ""
}

variable "ecs_cpu" {
  description = "CPU units for ECS task (1024 = 1 vCPU)"
  type        = number
  default     = 1024
}

variable "ecs_memory" {
  description = "Memory for ECS task in MB"
  type        = number
  default     = 4096
}

variable "ecr_image_tag" {
  description = "Docker image tag to use in task definition"
  type        = string
  default     = "latest"
}

