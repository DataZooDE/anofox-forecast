#!/bin/bash
set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Find the repository root (where .git is located)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Function to print colored output
print_error() {
    echo -e "${RED}ERROR:${NC} $1"
}

print_success() {
    echo -e "${GREEN}SUCCESS:${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}WARNING:${NC} $1"
}

print_info() {
    echo -e "${BLUE}INFO:${NC} $1"
}

print_header() {
    echo -e "\n${BLUE}===================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}===================================${NC}\n"
}

# Parse command line arguments
FIX_MODE=false
if [ "$1" == "--fix" ]; then
    FIX_MODE=true
fi

# Change to repository root
cd "$REPO_ROOT"

print_header "Code Quality Check"

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ] || [ ! -d "duckdb" ]; then
    print_error "This script must be run from the repository root or scripts directory"
    exit 1
fi

print_info "Repository root: $REPO_ROOT"

# Check Python 3
print_header "Checking Dependencies"

if ! command -v python3 &> /dev/null; then
    print_error "python3 is not installed"
    exit 1
fi
print_success "python3 found: $(python3 --version)"

# Check black
if ! command -v black &> /dev/null; then
    print_error "black is not installed"
    echo ""
    echo "To install required dependencies, run:"
    echo "  pip install \"black>=24\" clang_format==11.0.1 cmake-format"
    exit 1
fi

BLACK_VERSION=$(black --version 2>&1 | head -n 1)
BLACK_MAJOR=$(echo "$BLACK_VERSION" | grep -oP 'black, \K[0-9]+' || echo "0")
if [ "$BLACK_MAJOR" -lt 24 ]; then
    print_error "black version must be >= 24 (found: $BLACK_VERSION)"
    echo ""
    echo "To upgrade black, run:"
    echo "  pip install \"black>=24\""
    exit 1
fi
print_success "black found: $BLACK_VERSION"

# Check clang-format
if ! command -v clang-format &> /dev/null; then
    print_error "clang-format is not installed"
    echo ""
    echo "To install required dependencies, run:"
    echo "  pip install \"black>=24\" clang_format==11.0.1 cmake-format"
    exit 1
fi

CLANG_VERSION=$(clang-format --version 2>&1)
if [[ ! "$CLANG_VERSION" =~ "11." ]]; then
    print_warning "clang-format version should be 11.0.1 (found: $CLANG_VERSION)"
    echo "This may cause formatting differences from CI"
fi
print_success "clang-format found: $CLANG_VERSION"

# Check cmake-format
if ! command -v cmake-format &> /dev/null; then
    print_error "cmake-format is not installed"
    echo ""
    echo "To install required dependencies, run:"
    echo "  pip install \"black>=24\" clang_format==11.0.1 cmake-format"
    exit 1
fi
print_success "cmake-format found: $(cmake-format --version 2>&1 | head -n 1)"

# Run the format check or fix
print_header "Running Format Check"

if [ "$FIX_MODE" = true ]; then
    print_info "Running in FIX mode - will automatically fix formatting issues"
    echo ""
    
    if python3 duckdb/scripts/format.py --all --fix --noconfirm --directories src test; then
        print_success "All files have been formatted successfully!"
        exit 0
    else
        print_error "Format fix encountered errors"
        exit 1
    fi
else
    print_info "Running in CHECK mode - will only report formatting issues"
    echo ""
    echo "Command: python3 duckdb/scripts/format.py --all --check --directories src test"
    echo ""
    
    if python3 duckdb/scripts/format.py --all --check --directories src test; then
        print_success "All files are properly formatted!"
        exit 0
    else
        echo ""
        print_error "Format check failed - some files need formatting"
        echo ""
        echo "To fix these issues automatically, run:"
        echo "  ${GREEN}make format-fix${NC}"
        echo ""
        echo "Or use this script in fix mode:"
        echo "  ${GREEN}./scripts/check_code_quality.sh --fix${NC}"
        echo ""
        exit 1
    fi
fi

