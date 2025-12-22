output "ecr_repository_url" {
  description = "URL of the ECR repository"
  value       = aws_ecr_repository.benchmark.repository_url
}

output "ecr_repository_arn" {
  description = "ARN of the ECR repository"
  value       = aws_ecr_repository.benchmark.arn
}

output "ecs_cluster_name" {
  description = "Name of the ECS cluster"
  value       = aws_ecs_cluster.benchmark.name
}

output "ecs_cluster_arn" {
  description = "ARN of the ECS cluster"
  value       = aws_ecs_cluster.benchmark.arn
}

output "ecs_task_definition_arn" {
  description = "ARN of the ECS task definition"
  value       = aws_ecs_task_definition.benchmark.arn
}

output "ecs_task_definition_family" {
  description = "Family name of the ECS task definition"
  value       = aws_ecs_task_definition.benchmark.family
}

output "task_execution_role_arn" {
  description = "ARN of the ECS task execution role"
  value       = aws_iam_role.ecs_task_execution.arn
}

output "task_role_arn" {
  description = "ARN of the ECS task role"
  value       = aws_iam_role.ecs_task.arn
}

output "cloudwatch_log_group_name" {
  description = "Name of the CloudWatch log group"
  value       = aws_cloudwatch_log_group.benchmark.name
}

output "s3_bucket_name" {
  description = "Name of the S3 bucket (if created)"
  value       = var.create_s3_bucket ? aws_s3_bucket.results[0].id : null
}

output "s3_bucket_arn" {
  description = "ARN of the S3 bucket (if created)"
  value       = var.create_s3_bucket ? aws_s3_bucket.results[0].arn : null
}

