provider "aws" {
  region = var.aws_region
}

# Get current AWS account ID
data "aws_caller_identity" "current" {}

# ECR Repository
resource "aws_ecr_repository" "benchmark" {
  name                 = var.project_name
  image_tag_mutability = "MUTABLE"

  image_scanning_configuration {
    scan_on_push = true
  }

  tags = {
    Name        = var.project_name
    Environment = "benchmark"
  }
}

# ECS Cluster
resource "aws_ecs_cluster" "benchmark" {
  name = "${var.project_name}-cluster"

  setting {
    name  = "containerInsights"
    value = "enabled"
  }

  tags = {
    Name        = "${var.project_name}-cluster"
    Environment = "benchmark"
  }
}

# CloudWatch Log Group
resource "aws_cloudwatch_log_group" "benchmark" {
  name              = "/ecs/${var.project_name}"
  retention_in_days = 7

  tags = {
    Name        = "${var.project_name}-logs"
    Environment = "benchmark"
  }
}

# IAM Role for ECS Task Execution
resource "aws_iam_role" "ecs_task_execution" {
  name = "${var.project_name}-task-execution-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Action = "sts:AssumeRole"
        Effect = "Allow"
        Principal = {
          Service = "ecs-tasks.amazonaws.com"
        }
      }
    ]
  })

  tags = {
    Name        = "${var.project_name}-task-execution-role"
    Environment = "benchmark"
  }
}

# Attach AWS managed policy for ECS task execution
resource "aws_iam_role_policy_attachment" "ecs_task_execution" {
  role       = aws_iam_role.ecs_task_execution.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AmazonECSTaskExecutionRolePolicy"
}

# IAM Role for ECS Task (application role)
resource "aws_iam_role" "ecs_task" {
  name = "${var.project_name}-task-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Action = "sts:AssumeRole"
        Effect = "Allow"
        Principal = {
          Service = "ecs-tasks.amazonaws.com"
        }
      }
    ]
  })

  tags = {
    Name        = "${var.project_name}-task-role"
    Environment = "benchmark"
  }
}

# IAM Policy for S3 write access
resource "aws_iam_policy" "s3_write" {
  name        = "${var.project_name}-s3-write-policy"
  description = "Policy for ECS task to write benchmark results to S3"

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect = "Allow"
        Action = [
          "s3:PutObject",
          "s3:PutObjectAcl",
          "s3:GetObject"
        ]
        Resource = var.create_s3_bucket ? "${aws_s3_bucket.results[0].arn}/*" : "*"
      },
      {
        Effect = "Allow"
        Action = [
          "s3:ListBucket"
        ]
        Resource = var.create_s3_bucket ? aws_s3_bucket.results[0].arn : "*"
      }
    ]
  })
}

resource "aws_iam_role_policy_attachment" "ecs_task_s3" {
  role       = aws_iam_role.ecs_task.name
  policy_arn = aws_iam_policy.s3_write.arn
}

# S3 Bucket (optional)
resource "aws_s3_bucket" "results" {
  count  = var.create_s3_bucket ? 1 : 0
  bucket = var.s3_bucket_name != "" ? var.s3_bucket_name : "${var.project_name}-results-${data.aws_caller_identity.current.account_id}"

  tags = {
    Name        = "${var.project_name}-results"
    Environment = "benchmark"
  }
}

resource "aws_s3_bucket_versioning" "results" {
  count  = var.create_s3_bucket ? 1 : 0
  bucket = aws_s3_bucket.results[0].id

  versioning_configuration {
    status = "Disabled"
  }
}

# ECS Task Definition
resource "aws_ecs_task_definition" "benchmark" {
  family                   = "${var.project_name}-task"
  network_mode             = "awsvpc"
  requires_compatibilities = ["FARGATE"]
  cpu                      = var.ecs_cpu
  memory                   = var.ecs_memory
  execution_role_arn       = aws_iam_role.ecs_task_execution.arn
  task_role_arn            = aws_iam_role.ecs_task.arn

  container_definitions = jsonencode([
    {
      name      = "benchmark"
      image     = "${data.aws_caller_identity.current.account_id}.dkr.ecr.${var.aws_region}.amazonaws.com/${aws_ecr_repository.benchmark.name}:${var.ecr_image_tag}"
      cpu       = var.ecs_cpu
      memory    = var.ecs_memory
      essential = true

      environment = [
        {
          name  = "AWS_DEFAULT_REGION"
          value = var.aws_region
        }
      ]

      logConfiguration = {
        logDriver = "awslogs"
        options = {
          "awslogs-group"         = aws_cloudwatch_log_group.benchmark.name
          "awslogs-region"        = var.aws_region
          "awslogs-stream-prefix" = "ecs"
        }
      }

      command = ["uv", "run", "python", "run_all.py"]
    }
  ])

  tags = {
    Name        = "${var.project_name}-task"
    Environment = "benchmark"
  }
}

