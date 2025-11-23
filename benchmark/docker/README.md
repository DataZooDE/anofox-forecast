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

## Running on AWS with GitHub Actions

This section describes how to set up and use AWS infrastructure for running benchmarks via GitHub Actions.

### Infrastructure as Code Setup

We use **Terraform** to manage AWS infrastructure. This ensures:
- **Idempotent**: Running the setup multiple times is safe - if resources already exist and match the configuration, no changes are made
- **Version controlled**: Infrastructure changes are tracked in code
- **Reproducible**: Same configuration creates the same infrastructure every time

#### Prerequisites

- [Terraform](https://www.terraform.io/downloads) installed (>= 1.0)
- [AWS CLI](https://aws.amazon.com/cli/) installed and configured
- AWS credentials with permissions to create ECR, ECS, IAM, CloudWatch, and S3 resources

#### Setting Up AWS Infrastructure

1. **Run the setup script**:

```bash
cd benchmark/docker
./setup_aws_resources.sh --region us-east-1 --s3-bucket my-benchmark-results
```

The script will:
- Check for required tools (Terraform, AWS CLI)
- Initialize Terraform
- Show a plan of what will be created
- Ask for confirmation before applying changes

**Configuring Fargate Resources**:

You can customize CPU and memory for the ECS Fargate task using command-line flags:

```bash
# Use 2 vCPU and 8GB RAM
./setup_aws_resources.sh --cpu 2048 --memory 8192

# Use 4 vCPU and 16GB RAM
./setup_aws_resources.sh --cpu 4096 --memory 16384
```

**Fargate CPU/Memory Combinations**:

Fargate has specific valid CPU/memory combinations. Common options:

| CPU (vCPU) | CPU Units | Valid Memory (GB) |
|------------|-----------|-------------------|
| 0.25       | 256       | 0.5, 1, 2         |
| 0.5        | 512       | 1, 2, 3, 4        |
| 1          | 1024      | 2, 3, 4, 5, 6, 7, 8 |
| 2          | 2048      | 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 |
| 4          | 4096      | 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30 |
| 8          | 8192      | 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30 |
| 16         | 16384     | 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120 |

**Default**: 1 vCPU (1024 units) and 4GB (4096 MB) memory

**Note**: Memory must be specified in MB. For example, 4GB = 4096 MB.

2. **Review the Terraform plan**: The script shows what resources will be created/modified. If infrastructure already exists and matches the configuration, Terraform will report "No changes."

3. **Resources created**:
   - ECR repository (`anofox-benchmark`)
   - ECS cluster (`anofox-benchmark-cluster`)
   - IAM roles (task execution and task roles)
   - CloudWatch log group (`/ecs/anofox-benchmark`)
   - S3 bucket (optional, if `--s3-bucket` is provided)
   - ECS task definition

4. **Save the outputs**: After setup, Terraform outputs important values like ECR repository URI and ECS cluster name. These are used by the GitHub Actions workflow.

#### Manual Terraform Usage

You can also use Terraform directly:

```bash
cd benchmark/docker/terraform

# Initialize
terraform init

# Preview changes
terraform plan

# Apply changes
terraform apply

# View outputs
terraform output

# Destroy infrastructure (when no longer needed)
terraform destroy
```

**Configuring Fargate via Terraform Variables**:

You can set Fargate CPU and memory in several ways:

1. **Via terraform.tfvars file** (recommended):

```bash
# Copy the example file
cd benchmark/docker/terraform
cp terraform.tfvars.example terraform.tfvars

# Edit terraform.tfvars with your values
```

Example `terraform.tfvars`:

```hcl
aws_region = "us-east-1"
ecs_cpu = 2048      # 2 vCPU
ecs_memory = 8192   # 8GB (8192 MB)
create_s3_bucket = true
s3_bucket_name = "my-benchmark-results"
```

2. **Via command-line flags**:

```bash
terraform apply \
  -var="ecs_cpu=2048" \
  -var="ecs_memory=8192"
```

3. **Via environment variables** (prefix with `TF_VAR_`):

```bash
export TF_VAR_ecs_cpu=2048
export TF_VAR_ecs_memory=8192
terraform apply
```

4. **Via terraform apply -var-file**:

```bash
terraform apply -var-file="custom.tfvars"
```

**Available Terraform Variables**:

- `ecs_cpu`: CPU units (256, 512, 1024, 2048, 4096, 8192, 16384)
- `ecs_memory`: Memory in MB (must be valid for selected CPU)
- `aws_region`: AWS region (default: us-east-1)
- `project_name`: Project name for resource naming (default: anofox-benchmark)
- `create_s3_bucket`: Whether to create S3 bucket (default: false)
- `s3_bucket_name`: S3 bucket name (only if create_s3_bucket=true)
- `ecr_image_tag`: Docker image tag (default: latest)

**Updating Fargate Resources After Initial Setup**:

If you need to change CPU or memory after the infrastructure is already created:

1. Update `terraform.tfvars` (or use `-var` flags)
2. Run `terraform plan` to preview changes
3. Run `terraform apply` to update the task definition

Terraform will create a new task definition revision with the updated CPU/memory. The GitHub Actions workflow will automatically use the latest task definition revision.

### GitHub Actions Workflow

A GitHub Actions workflow is available to build the Docker image and optionally run benchmarks on AWS ECS.

#### Required GitHub Secrets

Configure these secrets in your GitHub repository settings:

- `AWS_ACCESS_KEY_ID`: AWS access key with permissions to push to ECR and run ECS tasks
- `AWS_SECRET_ACCESS_KEY`: AWS secret key

#### Triggering the Workflow

1. Go to **Actions** tab in your GitHub repository
2. Select **"Build and Deploy Benchmark to AWS"** workflow
3. Click **"Run workflow"**
4. Fill in the inputs:
   - **AWS Region**: AWS region (default: `us-east-1`)
   - **ECS Cluster Name**: Name of your ECS cluster (default: `anofox-benchmark-cluster`)
   - **S3 Bucket**: S3 bucket name for storing results
   - **Run ECS Task**: Check to automatically run the task after building
   - **Subnet IDs**: Comma-separated subnet IDs (required if running task)
   - **Security Group IDs**: Comma-separated security group IDs (required if running task)

#### Workflow Steps

The workflow will:
1. Checkout code
2. Configure AWS credentials
3. Login to Amazon ECR
4. Build Docker image (using community extension, no download needed)
5. Tag and push image to ECR
6. Optionally run ECS task if "Run ECS Task" is enabled

#### Monitoring

- **ECS Tasks**: Monitor in AWS Console → ECS → Clusters → Your Cluster → Tasks
- **CloudWatch Logs**: View logs at `/ecs/anofox-benchmark` log group
- **S3 Results**: Check your S3 bucket for benchmark results

## Running on AWS (Manual Setup)

For running benchmarks in the cloud manually, **AWS ECS (Elastic Container Service) with Fargate** is recommended. It allows you to run containers without managing servers (EC2 instances) and pay only for the resources used during execution.

> **Note**: For automated CI/CD, use the [GitHub Actions workflow](#github-actions-workflow) described above. The manual steps below are for reference or custom setups.

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
