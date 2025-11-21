# Running Benchmarks in Docker

This directory contains the configuration to run benchmarks in a Docker container.

## Prerequisites

- Docker installed
- For S3 upload: AWS credentials (`AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`)
- For GitHub artifact download: GitHub Token

## Building the Docker Image

You can build the image using `docker-compose`.

### Option 1: Build with Community Extension (Default)

Does not require any special tokens. The container will use the community extension available via DuckDB or you can mount a local one.

```bash
docker-compose build
```

### Option 2: Build with GitHub Artifact

Downloads the latest `anofox_forecast` extension from the `datazoode/anofox-forecast` repository.

```bash
# Set your GitHub token
export GITHUB_TOKEN=your_github_token

# Build with download enabled
DOWNLOAD_EXTENSION=true docker-compose build
```

## Running Benchmarks Locally

### Basic Run

Runs all benchmarks (`m4`, `m5`) and saves results to `./results` (mounted from host).

```bash
docker-compose up
```

### Running with S3 Upload

Set the `S3_BUCKET` environment variable to upload results automatically.

```bash
export S3_BUCKET=my-benchmark-results-bucket
docker-compose up
```

### Running with Custom Extension (Local)

If you have a locally built extension you want to test without downloading:

1. Uncomment the volume mount in `docker-compose.yml`:
   ```yaml
   # - ../../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension:/extension.duckdb_extension
   ```
2. Update `ANOFOX_EXTENSION_PATH` to point to the mounted path:
   ```yaml
   environment:
     - ANOFOX_EXTENSION_PATH=/extension.duckdb_extension
   ```

## Running on AWS

For running benchmarks in the cloud, **AWS ECS (Elastic Container Service) with Fargate** is recommended. It allows you to run containers without managing servers (EC2 instances) and pay only for the resources used during execution.

### 1. Create an ECR Repository

Create a repository in Amazon Elastic Container Registry (ECR) to store your Docker image.

```bash
aws ecr create-repository --repository-name anofox-benchmark
```

### 2. Authenticate and Push Image

Authenticate Docker to your ECR registry and push the image.

```bash
# Replace AWS_ACCOUNT_ID and REGION with your values
aws ecr get-login-password --region REGION | docker login --username AWS --password-stdin AWS_ACCOUNT_ID.dkr.ecr.REGION.amazonaws.com

# Tag your image (assuming you built it locally with docker-compose)
# Find the local image id/tag first, e.g., docker-benchmark:latest
docker tag docker-benchmark:latest AWS_ACCOUNT_ID.dkr.ecr.REGION.amazonaws.com/anofox-benchmark:latest

# Push the image
docker push AWS_ACCOUNT_ID.dkr.ecr.REGION.amazonaws.com/anofox-benchmark:latest
```

### 3. Run with ECS Fargate

You can use the AWS CLI to register a task definition and run it.

**A. Create Task Definition (`ecs-task-def.json`)**

Replace placeholders (Role ARNs, Image URI). ensure the `taskRoleArn` has permissions to write to your S3 bucket.

```json
{
    "family": "anofox-benchmark-task",
    "networkMode": "awsvpc",
    "containerDefinitions": [
        {
            "name": "benchmark",
            "image": "AWS_ACCOUNT_ID.dkr.ecr.REGION.amazonaws.com/anofox-benchmark:latest",
            "cpu": 1024,
            "memory": 4096,
            "essential": true,
            "environment": [
                { "name": "S3_BUCKET", "value": "your-results-bucket-name" },
                { "name": "AWS_DEFAULT_REGION", "value": "us-east-1" }
            ],
            "logConfiguration": {
                "logDriver": "awslogs",
                "options": {
                    "awslogs-group": "/ecs/anofox-benchmark",
                    "awslogs-region": "us-east-1",
                    "awslogs-stream-prefix": "ecs"
                }
            }
        }
    ],
    "requiresCompatibilities": ["FARGATE"],
    "cpu": "1024",
    "memory": "4096",
    "executionRoleArn": "arn:aws:iam::AWS_ACCOUNT_ID:role/ecsTaskExecutionRole",
    "taskRoleArn": "arn:aws:iam::AWS_ACCOUNT_ID:role/YourTaskRoleWithS3WriteAccess"
}
```

**B. Register and Run**

```bash
# Register the task definition
aws ecs register-task-definition --cli-input-json file://ecs-task-def.json

# Run the task (requires a valid Subnet ID and Security Group ID from your VPC)
aws ecs run-task \
    --cluster default \
    --launch-type FARGATE \
    --task-definition anofox-benchmark-task \
    --network-configuration "awsvpcConfiguration={subnets=[subnet-xxxxxx],securityGroups=[sg-xxxxxx],assignPublicIp=ENABLED}"
```

### AWS Recommendations

- **Service**: **ECS Fargate** is best for simplicity and cost-efficiency for ad-hoc benchmark runs. You don't need to manage EC2 instances.
- **Storage**: Results are uploaded to **S3**, so ephemeral container storage is sufficient.
- **Compute**: Adjust CPU/Memory in the task definition based on benchmark size. Start with 1 vCPU / 4GB RAM (cpu: 1024, memory: 4096).
