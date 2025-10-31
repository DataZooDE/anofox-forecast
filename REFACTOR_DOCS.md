# Refactoring: Documentation Build System

## Goal
Implement a build system for documentation that embeds SQL examples from separate files into Markdown documents and automatically tests them.

## Tasks

### 1. Prepare Project Structure

Create the following directory structure:
```
guides/
└── templates/
test/
└── sql/
scripts/
```

Note: 
- SQL example files will be placed in `test/sql/` so they are part of the test suite and can be easily validated
- Markdown templates (`.md.in` files) will be placed in `guides/templates/`
- Generated markdown files (`.md` files) will be placed in `guides/`

### 2. Analyze and Transform Markdown Files

For **each** `.md` file in the project (excluding node_modules, .git, build):

1. **Open the file** and search for SQL code blocks:
   ```markdown
   ```sql
   SELECT * FROM table;
   ```
   ```

2. **For each SQL block found:**
   - Extract the SQL code
   - Create a new file under `test/sql/` with a descriptive name
     - Example: `test/sql/basic_query.sql`
     - Use snake_case for filenames
     - Number multiple examples: `example_01.sql`, `example_02.sql`
     - If the context suggests a name (e.g., "Basic Query"), use that
   
3. **Create a template:**
   - Move the file to `guides/templates/` and rename: `filename.md` → `guides/templates/filename.md.in`
   - Replace each SQL code block with:
     ```markdown
     <!-- include: test/sql/example_name.sql -->
     ```
   - The generated markdown will be created at `guides/filename.md`

4. **Special cases:**
   - If a SQL block contains comments like `-- This is setup code`, use these for the filename
   - `README.md` in the project root should become `guides/templates/README.md.in` and generate to `guides/README.md`
   - Other root-level docs should also move to `guides/templates/` with `.md.in` extension
   - Keep all other content of the Markdown files unchanged

**Important:** After transformation, create a `README.md` in the project root that links to `guides/README.md` or points users to the documentation in the `guides/` folder.

### 2a. Extending Existing Guides with New Use Cases

**If the project already has some guides following this structure but they are not as extensive as desired:**

1. **Analyze the existing guides structure:**
   - Check if `guides/templates/` already exists
   - Check if `test/sql/` already exists with some examples
   - Identify which examples are already extracted and which are still embedded

2. **For guides that need more examples:**
   - Open each guide in `guides/templates/`
   - Look for sections that could benefit from examples but don't have them yet
   - Identify use cases that exist in other projects but are missing here

3. **Cross-pollinate examples from other projects:**
   - Review SQL examples from your other DuckDB extension projects
   - Identify examples that demonstrate similar functionality
   - Adapt these examples to work with the current extension's functionality
   - Create new SQL files in `test/sql/` for these adapted examples

4. **Add new sections to existing guides:**
   - Identify gaps in documentation (e.g., missing advanced use cases, performance tips, edge cases)
   - Add new sections to the `.md.in` templates
   - Create corresponding SQL examples in `test/sql/`
   - Reference the examples using `<!-- include: test/sql/example.sql -->` directives

5. **Organize examples by complexity:**
   ```
   test/sql/
   ├── 01_basic_usage.sql
   ├── 02_intermediate_filtering.sql
   ├── 03_advanced_aggregation.sql
   ├── 04_performance_optimization.sql
   └── 05_edge_cases.sql
   ```

6. **Create a coverage checklist:**
   Create `guides/templates/COVERAGE.md` to track what examples exist:
   ```markdown
   # Documentation Coverage
   
   ## Basic Usage
   - [x] Simple query example
   - [x] Loading data
   - [ ] Basic filtering (TODO)
   
   ## Advanced Features
   - [x] Complex joins
   - [ ] Window functions (TODO)
   - [ ] Custom aggregations (TODO)
   
   ## Performance
   - [ ] Query optimization tips (TODO)
   - [ ] Benchmarking examples (TODO)
   ```

7. **Standardize example structure:**
   Each SQL example should follow a consistent pattern:
   ```sql
   -- Example: [Brief description]
   -- Demonstrates: [Key concept being shown]
   -- Related: [Links to other examples if applicable]
   
   -- Setup (if needed)
   CREATE TABLE example_table (
       id INTEGER,
       value VARCHAR
   );
   
   -- Example query
   SELECT * FROM example_table
   WHERE value LIKE 'test%';
   
   -- Expected behavior:
   -- [Describe what this query demonstrates]
   ```

8. **Add navigation between guides:**
   In your template files, add navigation sections:
   ```markdown
   ## Related Guides
   - [Basic Usage](./basic-usage.md) - Start here if you're new
   - [Advanced Features](./advanced-features.md) - For power users
   - [Performance Tips](./performance-tips.md) - Optimization guide
   ```

9. **Reuse examples across guides:**
   The same SQL file can be included in multiple guide templates:
   ```markdown
   In guides/templates/getting-started.md.in:
   <!-- include: test/sql/basic_query.sql -->
   
   In guides/templates/performance.md.in:
   This basic query can be optimized:
   <!-- include: test/sql/basic_query.sql -->
   ```

10. **Document missing use cases:**
    Create `guides/templates/TODO.md` to track needed examples:
    ```markdown
    # Documentation TODO
    
    ## Missing Examples
    - [ ] Bulk insert operations
    - [ ] Transaction handling
    - [ ] Error recovery patterns
    
    ## Examples to Improve
    - [ ] Add error handling to `basic_query.sql`
    - [ ] Show performance comparison in aggregation examples
    
    ## New Guides Needed
    - [ ] Troubleshooting guide
    - [ ] Migration guide from other systems
    ```

### 3. Create Build Script

Create `scripts/build_docs.sh` with the following content:

```bash
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
    
    while IFS= read -r line; do
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
    done < "$input_file" > "$temp_file"
    
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
        ((file_count++))
    done < <(find "$TEMPLATES_DIR" -name "*.md.in" -type f)
    
    echo ""
    echo "✅ Built $file_count documentation file(s)"
    
    # Run markdownlint on generated files
    if ! lint_markdown; then
        exit 1
    fi
}

main "$@"
```

Make the script executable:
```bash
chmod +x scripts/build_docs.sh
```

### 4. Create Test Script

Create `scripts/test_sql_examples.sh`:

```bash
#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Configuration - adjust these to match your extension
EXTENSION_NAME="${EXTENSION_NAME:-your_extension_name}"
BUILD_DIR="${BUILD_DIR:-./build/release}"
SQL_DIR="${PROJECT_ROOT}/test/sql"

# Test a SQL file
test_sql_file() {
    local sql_file=$1
    local filename=$(basename "$sql_file")
    
    # Create temporary file with extension setup
    local test_file=$(mktemp)
    
    cat > "$test_file" <<EOF
-- Load extension if available
.bail on
INSTALL '${BUILD_DIR}/extension/${EXTENSION_NAME}/${EXTENSION_NAME}.duckdb_extension';
LOAD ${EXTENSION_NAME};

-- Run actual SQL
EOF
    
    cat "$sql_file" >> "$test_file"
    
    # Run test
    if duckdb :memory: < "$test_file" > /dev/null 2>&1; then
        echo "  ✅ $filename"
        rm "$test_file"
        return 0
    else
        echo "  ❌ $filename FAILED"
        echo "     Error output:"
        duckdb :memory: < "$test_file" 2>&1 | head -n 20
        rm "$test_file"
        return 1
    fi
}

# Main function
main() {
    echo "🧪 Testing SQL example files..."
    echo ""
    
    local total=0
    local failed=0
    
    # Test all SQL files in test/sql/
    while IFS= read -r sql_file; do
        ((total++))
        if ! test_sql_file "$sql_file"; then
            ((failed++))
        fi
    done < <(find "$SQL_DIR" -name "*.sql" -type f 2>/dev/null)
    
    echo ""
    echo "================================"
    echo "Total: $total"
    echo "Passed: $((total - failed))"
    echo "Failed: $failed"
    echo "================================"
    
    if [ $failed -gt 0 ]; then
        exit 1
    fi
}

main "$@"
```

Make the script executable:
```bash
chmod +x scripts/test_sql_examples.sh
```

### 5. Create Hook Installation Script

Create `scripts/install_hooks.sh`:

```bash
#!/bin/bash

set -euo pipefail

HOOK_DIR=".git/hooks"

echo "📦 Installing git hooks..."

# Pre-commit Hook
cat > "$HOOK_DIR/pre-commit" <<'EOF'
#!/bin/bash

echo "🔨 Building documentation..."
bash scripts/build_docs.sh

if [ $? -ne 0 ]; then
    echo "❌ Documentation build failed!"
    exit 1
fi

echo "📝 Linting markdown files..."
if command -v markdownlint &> /dev/null; then
    if ! markdownlint 'guides/**/*.md' --ignore node_modules 2>/dev/null; then
        echo "❌ Markdownlint found issues!"
        echo "Run 'markdownlint --fix guides/**/*.md' to auto-fix some issues"
        exit 1
    fi
    echo "✅ Markdown files passed linting"
else
    echo "⚠️  Warning: markdownlint not found, skipping markdown linting"
    echo "   Install with: npm install -g markdownlint-cli"
fi

echo "🧪 Testing SQL examples..."
bash scripts/test_sql_examples.sh

if [ $? -ne 0 ]; then
    echo "❌ SQL example tests failed!"
    echo "Fix the examples or use 'git commit --no-verify' to skip"
    exit 1
fi

# Stage the generated markdown files for commit
# This ensures GitHub visitors see the complete documentation
if [ -d "guides" ]; then
    find guides -name "*.md" -not -name "*.md.in" -type f -exec git add {} \;
fi

echo "✅ Documentation built, linted, tested, and staged for commit"
EOF

chmod +x "$HOOK_DIR/pre-commit"

echo "✅ Git hooks installed successfully"
echo ""
echo "The pre-commit hook will now:"
echo "  1. Build documentation from templates in guides/templates/"
echo "  2. Lint markdown files in guides/ with markdownlint"
echo "  3. Test all SQL examples"
echo "  4. Stage generated .md files in guides/ for commit (visible on GitHub)"
echo ""
echo "To skip hooks during commit, use: git commit --no-verify"
```

Make the script executable:
```bash
chmod +x scripts/install_hooks.sh
```

### 6. Update Makefile

Add or update the following targets in the `Makefile`:

```makefile
.PHONY: docs test-docs clean-docs install-hooks lint-docs

# Build documentation from templates
docs:
	@bash scripts/build_docs.sh

# Test SQL examples
test-docs:
	@bash scripts/test_sql_examples.sh

# Lint markdown files
lint-docs:
	@echo "📝 Linting markdown files..."
	@markdownlint 'guides/**/*.md' --ignore node_modules

# Clean generated documentation
clean-docs:
	@echo "🧹 Cleaning generated documentation..."
	@find guides -name "*.md" -not -name "*.md.in" -type f -exec rm -f {} \;
	@echo "✅ Cleaned"

# Install git hooks
install-hooks:
	@bash scripts/install_hooks.sh

# Add docs to existing build target
build: docs
	# ... existing build commands

# Add test-docs to existing test target
test: test-docs
	# ... existing test commands
```

### 7. Setup Markdownlint

Create `.markdownlint.json` in the project root:

```json
{
  "default": true,
  "MD013": {
    "line_length": 120,
    "code_blocks": false,
    "tables": false
  },
  "MD033": {
    "allowed_elements": ["antml:cite"]
  },
  "MD041": false
}
```

Install markdownlint-cli (if not already installed):
```bash
npm install -g markdownlint-cli
```

Or add to package.json if your project uses npm:
```json
{
  "devDependencies": {
    "markdownlint-cli": "^0.37.0"
  },
  "scripts": {
    "lint:md": "markdownlint 'guides/**/*.md' --ignore node_modules"
  }
}
```

### 8. Update .gitignore (Optional)

Since the generated `.md` files need to be committed for GitHub visitors, we **do not** ignore them. However, you may want to add a comment to your `.gitignore` to document this:

```gitignore
# Note: Generated .md files in guides/ ARE committed (not ignored)
# They are built from guides/templates/*.md.in and need to be visible on GitHub
# The build process is automated via pre-commit hooks

# Only ignore these documentation-related items if needed:
# /guides/build/
# *.md.backup
```

**Important:** The generated `.md` files in `guides/` will be version-controlled and visible on GitHub, which is the desired behavior for public documentation.

### 9. Verification Steps

After completing the refactoring:

1. **Install markdownlint:**
   ```bash
   npm install -g markdownlint-cli
   ```

2. **Build documentation:**
   ```bash
   make docs
   ```

3. **Verify generated files:**
   - Check that `.md` files were created in `guides/` from templates in `guides/templates/`
   - Verify that SQL code blocks are properly embedded
   - Ensure the folder structure looks like:
     ```
     guides/
     ├── templates/
     │   ├── getting-started.md.in
     │   └── advanced-usage.md.in
     ├── getting-started.md
     └── advanced-usage.md
     ```

4. **Lint markdown files:**
   ```bash
   make lint-docs
   # Or fix issues automatically:
   markdownlint --fix 'guides/**/*.md'
   ```

5. **Test SQL examples:**
   ```bash
   make test-docs
   ```

6. **Install hooks:**
   ```bash
   make install-hooks
   ```

7. **Test the workflow:**
   ```bash
   # Make a change to a SQL file
   echo "SELECT 1 + 1;" > test/sql/test.sql
   
   # Add reference to a template
   echo "<!-- include: test/sql/test.sql -->" >> guides/templates/example.md.in
   
   # Build, lint, and test
   make docs lint-docs test-docs
   
   # Check the generated file
   cat guides/example.md
   ```

8. **Create root README (if needed):**
   If your main README was moved to `guides/templates/README.md.in`, create a simple root `README.md` that links to the guides:
   ```bash
   cat > README.md <<'EOF'
   # Project Name
   
   For full documentation, see the [guides](./guides/) directory.
   
   - [Getting Started](./guides/getting-started.md)
   - [Advanced Usage](./guides/advanced-usage.md)
   EOF
   ```

## Important Notes

- **Preserve context:** When extracting SQL blocks, look at surrounding text to determine good filenames
- **Don't modify SQL:** Copy SQL code exactly as-is into separate files
- **Template naming:** Always use `.md.in` extension for templates in `guides/templates/`
- **Output location:** Generated files go to `guides/`, templates stay in `guides/templates/`
- **Test location:** All SQL examples go in `test/sql/` to be part of the test suite
- **Markdownlint:** Always run markdownlint on any markdown files created or edited
- **Lint configuration:** Adjust `.markdownlint.json` as needed for your project's style
- **Test before commit:** Ensure all SQL examples run successfully
- **Document changes:** Update the main README with information about the new build system and link to guides

## Extending Documentation in Existing Projects

If you're working on a project that already has the documentation build system set up but needs more comprehensive guides, follow this process:

### Phase 1: Assess Current State

1. **Inventory existing documentation:**
   ```bash
   # List all existing templates
   find guides/templates -name "*.md.in"
   
   # List all existing SQL examples
   find test/sql -name "*.sql"
   
   # Check which guides exist
   ls -la guides/
   ```

2. **Identify gaps:**
   - What topics are missing?
   - Which existing guides are too brief?
   - What examples would be helpful but don't exist?
   - Compare with your other (more comprehensive) extension project

3. **Create a documentation plan:**
   ```markdown
   # Documentation Enhancement Plan
   
   ## Current State
   - Basic usage guide (sparse)
   - Installation guide (complete)
   
   ## Gaps Identified
   - No advanced features guide
   - Missing performance optimization examples
   - No troubleshooting section
   - Edge cases not documented
   
   ## Examples Needed
   - Complex query patterns
   - Integration with other DuckDB features
   - Error handling scenarios
   ```

### Phase 2: Cross-Reference with Comprehensive Project

1. **List topics from comprehensive project:**
   ```bash
   # In your comprehensive extension project
   cd /path/to/comprehensive-extension
   find guides/templates -name "*.md.in" -exec basename {} \; | sort
   ```

2. **Identify applicable patterns:**
   - Which examples from the comprehensive project apply to this extension?
   - What functionality is similar between the two extensions?
   - Which patterns can be adapted with minimal changes?

3. **Create a mapping document:**
   ```markdown
   # Documentation Migration Plan
   
   ## From Comprehensive Project → This Project
   
   ### Directly Applicable
   - Basic query patterns → Adapt to our syntax
   - Performance tips → Same principles apply
   - Error handling → Reuse structure
   
   ### Needs Adaptation
   - Advanced aggregations → Different function names
   - Data types → Our types differ slightly
   
   ### Not Applicable
   - Specific feature X → We don't have this
   ```

### Phase 3: Add New Examples

1. **Create new SQL examples:**
   ```bash
   # Add examples progressively
   vim test/sql/advanced_filtering.sql
   vim test/sql/performance_comparison.sql
   vim test/sql/edge_case_nulls.sql
   ```

2. **Follow consistent structure:**
   ```sql
   -- Example: [Title]
   -- Purpose: [What this demonstrates]
   -- Difficulty: [Basic/Intermediate/Advanced]
   -- Related: [Other example files]
   
   -- Setup
   CREATE TABLE IF NOT EXISTS demo_table AS
   SELECT * FROM (VALUES 
       (1, 'Alice', 100),
       (2, 'Bob', 200)
   ) t(id, name, value);
   
   -- Main example
   SELECT 
       name,
       value,
       value * 1.1 AS adjusted_value
   FROM demo_table
   WHERE value > 50;
   
   -- Cleanup
   DROP TABLE IF EXISTS demo_table;
   ```

3. **Test each example:**
   ```bash
   # Ensure each example runs successfully
   duckdb :memory: < test/sql/advanced_filtering.sql
   ```

### Phase 4: Enhance Templates

1. **Add new sections to existing templates:**
   ```bash
   vim guides/templates/user-guide.md.in
   ```
   
   Add sections like:
   ```markdown
   ## Advanced Use Cases
   
   ### Complex Filtering
   
   Here's how to handle complex filtering scenarios:
   
   <!-- include: test/sql/advanced_filtering.sql -->
   
   ### Performance Optimization
   
   For better performance, consider these patterns:
   
   <!-- include: test/sql/performance_comparison.sql -->
   ```

2. **Create new guide templates:**
   ```bash
   # Create new comprehensive guides
   vim guides/templates/advanced-features.md.in
   vim guides/templates/performance-tuning.md.in
   vim guides/templates/troubleshooting.md.in
   ```

3. **Structure new guides:**
   ```markdown
   # Advanced Features Guide
   
   This guide covers advanced usage patterns for [extension name].
   
   ## Table of Contents
   - [Feature 1](#feature-1)
   - [Feature 2](#feature-2)
   - [Performance Tips](#performance-tips)
   
   ## Feature 1
   
   [Explanation]
   
   <!-- include: test/sql/feature1_basic.sql -->
   
   ### Advanced Usage
   
   <!-- include: test/sql/feature1_advanced.sql -->
   
   ## Feature 2
   
   [Explanation]
   
   <!-- include: test/sql/feature2_example.sql -->
   ```

### Phase 5: Improve Organization

1. **Rename examples for clarity:**
   ```bash
   # Use numbered prefixes for ordering
   mv test/sql/query.sql test/sql/01_basic_query.sql
   mv test/sql/advanced.sql test/sql/02_advanced_query.sql
   ```

2. **Group related examples:**
   ```
   test/sql/
   ├── basics/
   │   ├── 01_simple_select.sql
   │   ├── 02_filtering.sql
   │   └── 03_sorting.sql
   ├── advanced/
   │   ├── 01_complex_joins.sql
   │   ├── 02_window_functions.sql
   │   └── 03_recursive_queries.sql
   └── performance/
       ├── 01_indexing_tips.sql
       └── 02_query_optimization.sql
   ```
   
   Update includes accordingly:
   ```markdown
   <!-- include: test/sql/basics/01_simple_select.sql -->
   ```

3. **Create an example index:**
   ```bash
   vim test/sql/README.md
   ```
   
   ```markdown
   # SQL Examples Index
   
   ## Basics
   - `basics/01_simple_select.sql` - Basic SELECT queries
   - `basics/02_filtering.sql` - WHERE clause examples
   - `basics/03_sorting.sql` - ORDER BY examples
   
   ## Advanced
   - `advanced/01_complex_joins.sql` - Multi-table joins
   - `advanced/02_window_functions.sql` - Window function examples
   
   ## Performance
   - `performance/01_indexing_tips.sql` - Index usage
   - `performance/02_query_optimization.sql` - Query optimization
   
   ## Testing
   All examples are tested automatically via `make test-docs`.
   ```

### Phase 6: Add Cross-References

1. **Link between guides:**
   ```markdown
   In guides/templates/user-guide.md.in:
   
   For performance tips, see the [Performance Tuning Guide](./performance-tuning.md).
   
   In guides/templates/performance-tuning.md.in:
   
   Before optimizing, make sure you understand the basics in the [User Guide](./user-guide.md).
   ```

2. **Add breadcrumbs:**
   ```markdown
   # Advanced Features
   
   **Navigation:** [Home](./README.md) → [User Guide](./user-guide.md) → Advanced Features
   ```

3. **Create a documentation hub:**
   ```markdown
   In guides/templates/README.md.in:
   
   # Documentation Hub
   
   ## Getting Started
   - [Installation](./installation.md) - How to install
   - [Quick Start](./quick-start.md) - 5-minute tutorial
   
   ## User Guides
   - [Basic Usage](./user-guide.md) - Core features
   - [Advanced Features](./advanced-features.md) - Power user guide
   
   ## Reference
   - [API Reference](./api-reference.md) - Function documentation
   - [SQL Examples](./examples.md) - Example gallery
   
   ## Operations
   - [Performance Tuning](./performance-tuning.md) - Optimization guide
   - [Troubleshooting](./troubleshooting.md) - Common issues
   ```

### Phase 7: Validate and Test

1. **Build all documentation:**
   ```bash
   make docs
   ```

2. **Review generated guides:**
   ```bash
   # Check that all guides are generated
   ls -la guides/
   
   # Review a few key guides
   less guides/user-guide.md
   less guides/advanced-features.md
   ```

3. **Test all examples:**
   ```bash
   make test-docs
   ```

4. **Check for broken links:**
   ```bash
   # Install markdown link checker
   npm install -g markdown-link-check
   
   # Check all generated guides
   find guides -name "*.md" -not -name "*.md.in" -exec markdown-link-check {} \;
   ```

5. **Lint documentation:**
   ```bash
   make lint-docs
   ```

### Phase 8: Document the Enhancement

1. **Update the main README:**
   ```markdown
   ## Documentation
   
   Comprehensive documentation is available in the [guides](./guides/) directory:
   
   - **[Quick Start](./guides/quick-start.md)** - Get started in 5 minutes
   - **[User Guide](./guides/user-guide.md)** - Complete feature overview
   - **[Advanced Features](./guides/advanced-features.md)** - Advanced usage patterns
   - **[Performance Guide](./guides/performance-tuning.md)** - Optimization tips
   - **[Troubleshooting](./guides/troubleshooting.md)** - Common issues and solutions
   
   All examples in the documentation are tested automatically to ensure they work.
   ```

2. **Create a changelog entry:**
   ```markdown
   ## [Unreleased]
   
   ### Documentation
   - Added comprehensive advanced features guide
   - Enhanced performance tuning documentation with benchmarks
   - Added troubleshooting guide with common issues
   - Reorganized SQL examples by topic
   - Added cross-references between guides
   ```

### Example: Extending a Sparse Guide

**Before (guides/templates/user-guide.md.in):**
```markdown
# User Guide

## Basic Usage

You can query data like this:

<!-- include: test/sql/basic_query.sql -->

That's it!
```

**After (guides/templates/user-guide.md.in):**
```markdown
# User Guide

Complete guide to using [Extension Name].

**Navigation:** [Home](./README.md) → User Guide

## Table of Contents
- [Basic Usage](#basic-usage)
- [Filtering Data](#filtering-data)
- [Aggregations](#aggregations)
- [Performance Tips](#performance-tips)
- [Next Steps](#next-steps)

## Basic Usage

Start with a simple query:

<!-- include: test/sql/01_basic_query.sql -->

## Filtering Data

### Simple Filtering

Filter rows using WHERE clauses:

<!-- include: test/sql/02_simple_filtering.sql -->

### Complex Filtering

Combine multiple conditions:

<!-- include: test/sql/03_complex_filtering.sql -->

### Handling NULL Values

Special considerations for NULL values:

<!-- include: test/sql/04_null_handling.sql -->

## Aggregations

### Basic Aggregations

Count, sum, and average:

<!-- include: test/sql/05_basic_aggregations.sql -->

### Grouped Aggregations

Group data before aggregating:

<!-- include: test/sql/06_grouped_aggregations.sql -->

## Performance Tips

For optimal performance:

1. **Use appropriate filters** - See [Performance Guide](./performance-tuning.md)
   <!-- include: test/sql/07_performance_filtering.sql -->

2. **Leverage indexes** - When available
   <!-- include: test/sql/08_index_usage.sql -->

## Next Steps

- Learn about [Advanced Features](./advanced-features.md)
- Explore [Performance Tuning](./performance-tuning.md)
- Check out [Common Patterns](./patterns.md)
```

Now you have comprehensive instructions for extending sparse documentation! 🚀

## Expected Outcome

After this refactoring:
- ✅ All SQL examples are in separate, testable files under `test/sql/`
- ✅ SQL examples are part of the test suite and validated automatically
- ✅ Markdown templates are in `guides/templates/` with `.md.in` extension
- ✅ Generated markdown files are in `guides/` directory
- ✅ Build script generates final `.md` files with embedded SQL
- ✅ All markdown files are automatically linted with markdownlint
- ✅ Generated `.md` files in `guides/` are committed to Git and visible on GitHub
- ✅ Test script validates all SQL examples work correctly
- ✅ Git hooks automate the build-lint-test process before each commit
- ✅ GitHub visitors see complete, rendered, properly formatted documentation with working examples
- ✅ Clear separation between source (templates) and output (generated docs)
- ✅ The system is maintainable and easy to extend

## Project Structure After Refactoring

```
project/
├── guides/
│   ├── templates/          # Source templates
│   │   ├── getting-started.md.in
│   │   ├── advanced-usage.md.in
│   │   └── README.md.in
│   ├── getting-started.md  # Generated (committed)
│   ├── advanced-usage.md   # Generated (committed)
│   └── README.md          # Generated (committed)
├── test/
│   └── sql/               # Testable SQL examples
│       ├── basic_query.sql
│       ├── advanced_join.sql
│       └── create_table.sql
├── scripts/
│   ├── build_docs.sh      # Builds guides from templates
│   ├── test_sql_examples.sh
│   └── install_hooks.sh
├── README.md              # Root readme (links to guides/)
└── Makefile
```

## Workflow

The typical development workflow will be:

1. **Edit SQL examples:** Modify files in `test/sql/`
2. **Edit templates:** Update `.md.in` files in `guides/templates/` with documentation changes
3. **Commit changes:** Git hook automatically:
   - Builds `.md` files from templates to `guides/`
   - Lints all markdown files with markdownlint
   - Tests all SQL examples
   - Stages generated `.md` files in `guides/`
   - Commits both templates and generated files
4. **Push to GitHub:** Visitors see the complete, properly formatted documentation in `guides/` with embedded SQL examples
