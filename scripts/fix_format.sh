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
FIX_MARKDOWN=false
if [ "$1" == "--markdown" ] || [ "$2" == "--markdown" ]; then
    FIX_MARKDOWN=true
fi

# Change to repository root
cd "$REPO_ROOT"

print_header "Format Fixing Script"

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

# Check if format.py exists
if [ ! -f "duckdb/scripts/format.py" ]; then
    print_error "duckdb/scripts/format.py not found"
    print_info "Make sure the duckdb submodule is initialized"
    exit 1
fi
print_success "duckdb/scripts/format.py found"

# Check for markdown dependencies if needed
if [ "$FIX_MARKDOWN" = true ]; then
    if ! command -v markdownlint &> /dev/null; then
        print_error "markdownlint is not installed"
        echo ""
        echo "To install markdownlint, run:"
        echo "  npm install -g markdownlint-cli"
        exit 1
    fi
    print_success "markdownlint found: $(markdownlint --version)"
fi

# Fix code formatting
print_header "Fixing Code Formatting"
print_info "Running: python3 duckdb/scripts/format.py --all --fix --noconfirm --directories src test"
echo ""

if python3 duckdb/scripts/format.py --all --fix --noconfirm --directories src test; then
    print_success "Code formatting completed successfully!"
else
    print_error "Code formatting encountered errors"
    exit 1
fi

# Fix markdown if requested
if [ "$FIX_MARKDOWN" = true ]; then
    print_header "Fixing Markdown Formatting"
    print_info "Running: markdownlint 'guides/**/*.md' --fix --ignore node_modules"
    echo ""
    
    if markdownlint 'guides/**/*.md' --fix --ignore node_modules; then
        print_success "Markdown formatting completed successfully!"
    else
        print_warning "Markdown linting reported some issues"
        echo ""
        echo "Some markdown issues may need manual fixing"
    fi
fi

# Summary
print_header "Summary"
print_success "Format fixing completed!"
echo ""
echo "Changes made:"
echo "  - Code formatting (C++, Python, CMake) in src/ and test/"
if [ "$FIX_MARKDOWN" = true ]; then
    echo "  - Markdown formatting in guides/"
fi
echo ""
echo "Review the changes with: ${GREEN}git diff${NC}"
echo ""
echo "Tip: To also fix markdown files, run:"
echo "  ${GREEN}./scripts/fix_format.sh --markdown${NC}"

