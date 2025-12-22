#!/bin/bash
set -e

# Script to set up AWS infrastructure for benchmark runs using Terraform

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TERRAFORM_DIR="${SCRIPT_DIR}/terraform"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if Terraform is installed
check_terraform() {
    if ! command -v terraform &> /dev/null; then
        print_error "Terraform is not installed. Please install Terraform first:"
        echo "  https://www.terraform.io/downloads"
        exit 1
    fi
    if command -v jq &> /dev/null; then
        TERRAFORM_VERSION=$(terraform version -json | jq -r '.terraform_version' 2>/dev/null || terraform version | head -n 1)
    else
        TERRAFORM_VERSION=$(terraform version | head -n 1)
    fi
    print_info "Terraform version: ${TERRAFORM_VERSION}"
}

# Check if AWS CLI is installed
check_aws_cli() {
    if ! command -v aws &> /dev/null; then
        print_error "AWS CLI is not installed. Please install AWS CLI first:"
        echo "  https://aws.amazon.com/cli/"
        exit 1
    fi
    print_info "AWS CLI found"
}

# Check AWS credentials
check_aws_credentials() {
    if ! aws sts get-caller-identity &> /dev/null; then
        print_error "AWS credentials not configured. Please configure AWS credentials:"
        echo "  aws configure"
        echo "  or set AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY environment variables"
        exit 1
    fi
    ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
    print_info "AWS Account ID: ${ACCOUNT_ID}"
}

# Parse command line arguments
AWS_REGION="${AWS_REGION:-us-east-1}"
CREATE_S3_BUCKET=false
S3_BUCKET_NAME=""
ECS_CPU=1024
ECS_MEMORY=4096

usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Set up AWS infrastructure for benchmark runs using Terraform.

OPTIONS:
    -r, --region REGION          AWS region (default: us-east-1)
    -s, --s3-bucket NAME         Create S3 bucket with this name for results
    -c, --cpu CPU                ECS task CPU units (1024 = 1 vCPU, default: 1024)
    -m, --memory MEMORY          ECS task memory in MB (default: 4096)
    -h, --help                   Show this help message

EXAMPLES:
    # Basic setup (no S3 bucket)
    $0 --region us-east-1

    # Setup with S3 bucket
    $0 --region us-east-1 --s3-bucket my-benchmark-results

    # Setup with custom CPU/memory
    $0 --cpu 2048 --memory 8192

ENVIRONMENT VARIABLES:
    AWS_REGION                   AWS region (overridden by --region)
    AWS_ACCESS_KEY_ID            AWS access key
    AWS_SECRET_ACCESS_KEY        AWS secret key

NOTES:
    - Terraform will show a plan before applying changes
    - If infrastructure already exists and matches the configuration, no changes will be made
    - Run 'terraform plan' in ${TERRAFORM_DIR} to preview changes without applying
EOF
    exit 1
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -r|--region)
            AWS_REGION="$2"
            shift 2
            ;;
        -s|--s3-bucket)
            CREATE_S3_BUCKET=true
            S3_BUCKET_NAME="$2"
            shift 2
            ;;
        -c|--cpu)
            ECS_CPU="$2"
            shift 2
            ;;
        -m|--memory)
            ECS_MEMORY="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            ;;
    esac
done

# Main execution
main() {
    print_info "Starting AWS infrastructure setup..."
    print_info "Working directory: ${TERRAFORM_DIR}"

    # Pre-flight checks
    check_terraform
    check_aws_cli
    check_aws_credentials

    # Change to Terraform directory
    cd "${TERRAFORM_DIR}"

    # Initialize Terraform
    print_info "Initializing Terraform..."
    terraform init

    # Create terraform.tfvars if it doesn't exist
    if [ ! -f terraform.tfvars ]; then
        print_info "Creating terraform.tfvars..."
        cat > terraform.tfvars << EOF
aws_region = "${AWS_REGION}"
create_s3_bucket = ${CREATE_S3_BUCKET}
s3_bucket_name = "${S3_BUCKET_NAME}"
ecs_cpu = ${ECS_CPU}
ecs_memory = ${ECS_MEMORY}
EOF
    else
        print_warn "terraform.tfvars already exists. Update it manually if needed."
    fi

    # Plan
    print_info "Running Terraform plan..."
    terraform plan -out=tfplan

    # Ask for confirmation
    echo ""
    print_warn "Review the plan above. This will create/modify AWS resources."
    read -p "Do you want to apply these changes? (yes/no): " -r
    echo
    if [[ ! $REPLY =~ ^[Yy][Ee][Ss]$ ]]; then
        print_info "Aborted by user."
        exit 0
    fi

    # Apply
    print_info "Applying Terraform configuration..."
    terraform apply tfplan

    # Show outputs
    print_info "Infrastructure setup complete! Outputs:"
    echo ""
    terraform output

    # Cleanup plan file
    rm -f tfplan

    print_info "Setup complete!"
    print_info "Next steps:"
    echo "  1. Configure GitHub Actions secrets:"
    echo "     - AWS_ACCESS_KEY_ID"
    echo "     - AWS_SECRET_ACCESS_KEY"
    echo "  2. Trigger the workflow from GitHub Actions UI"
    echo "  3. Monitor CloudWatch logs: ${TERRAFORM_DIR}/terraform output cloudwatch_log_group_name"
}

main "$@"

