#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
TEMPLATES_DIR="${PROJECT_ROOT}/guides/templates"
OUTPUT_DIR="${PROJECT_ROOT}/guides"

# Process a template file
process_template() {
    local input_file=$1
    local relative_path="${input_file#$TEMPLATES_DIR/}"
    local output_file="${OUTPUT_DIR}/${relative_path%.in}"  # Remove .in extension

    echo "🔨 Building: $(basename $input_file) -> $output_file"

    # Create output directory if it doesn't exist
    mkdir -p "$(dirname "$output_file")"

    local temp_file=$(mktemp)

    while IFS= read -r line <&3; do
        # Check for include comment
        if [[ $line =~ \<!--[[:space:]]*include:[[:space:]]*([^[:space:]]+)[[:space:]]*--\> ]]; then
            local sql_file="${PROJECT_ROOT}/${BASH_REMATCH[1]}"

            if [ -f "$sql_file" ]; then
                echo '```sql'
                cat "$sql_file"
                echo '```'
            else
                echo "⚠️  Warning: File not found: $sql_file" >&2
                echo "$line"  # Keep original comment
            fi
        else
            echo "$line"
        fi
    done 3< "$input_file" > "$temp_file"

    mv "$temp_file" "$output_file"
    echo "✅ Generated: $output_file"
}

# Lint markdown files
lint_markdown() {
    echo ""
    echo "📝 Running markdownlint on generated files..."

    # Check if markdownlint is available
    if ! command -v markdownlint &> /dev/null; then
        echo "⚠️  Warning: markdownlint not found. Install with: npm install -g markdownlint-cli"
        return 0
    fi

    # Lint all generated markdown files in guides/
    local lint_failed=0
    if [ -d "$OUTPUT_DIR" ]; then
        while IFS= read -r md_file; do
            if ! markdownlint "$md_file"; then
                lint_failed=1
            fi
        done < <(find "$OUTPUT_DIR" -name "*.md" -not -name "*.md.in" -type f)
    fi

    if [ $lint_failed -eq 1 ]; then
        echo "❌ Markdownlint found issues. Please fix them."
        return 1
    fi

    echo "✅ All markdown files passed linting"
    return 0
}

# Main function
main() {
    echo "📚 Building documentation..."
    echo ""

    # Check if templates directory exists
    if [ ! -d "$TEMPLATES_DIR" ]; then
        echo "❌ Templates directory not found: $TEMPLATES_DIR"
        exit 1
    fi

    # Create output directory if it doesn't exist
    mkdir -p "$OUTPUT_DIR"

    # Find all .in files in templates directory
    local file_count=0
    while IFS= read -r template; do
        process_template "$template"
        file_count=$((file_count + 1))
    done < <(find "$TEMPLATES_DIR" -name "*.md.in" -type f)

    echo ""
    echo "✅ Built $file_count documentation file(s)"

    # Run markdownlint on generated files (disabled for now)
    # if ! lint_markdown; then
    #     exit 1
    # fi
    lint_markdown || true
}

main "$@"
