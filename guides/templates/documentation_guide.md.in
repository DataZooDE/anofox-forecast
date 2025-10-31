# Documentation System Guide

This guide explains how to maintain and extend the documentation for the Anofox Forecast extension.

## Overview

The documentation uses a **template-based build system** where:

1. SQL examples are stored as separate testable files
2. Markdown templates reference these SQL files
3. A build script generates final documentation
4. Everything is tested automatically

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Documentation Flow                    │
└─────────────────────────────────────────────────────────┘

    EDIT                    BUILD                   COMMIT
    ====                    =====                   ======

guides/templates/      scripts/build_docs.sh      guides/
├── *.md.in       ──>  processes templates   ──>  ├── *.md
└── SQL includes       embeds SQL code            └── (generated)

test/sql/docs_examples/
└── *.sql              tested automatically        (committed)
```

## Directory Structure

```
anofox-forecast/
├── guides/
│   ├── templates/              # 📝 EDIT THESE
│   │   ├── *.md.in            # Template files with include directives
│   │   └── 00_README.md.in       # Main documentation template
│   └── *.md                   # ✅ Generated (DO NOT EDIT)
│
├── test/sql/docs_examples/    # 📝 EDIT THESE
│   ├── 01_quickstart_*.sql   # SQL examples by guide
│   ├── 10_api_reference_*.sql
│   └── 00_README.md             # Examples index
│
├── scripts/
│   ├── build_docs.sh         # Build system
│   ├── test_sql_examples.sh  # Test runner
│   └── install_hooks.sh      # Git hooks installer
│
└── .markdownlint.json        # Linting configuration
```

## Quick Reference

### Common Tasks

```bash
# Build documentation
make docs

# Test SQL examples
make test-docs

# Lint markdown
make lint-docs

# Clean generated files
make clean-docs

# Install git hooks
make install-hooks
```

### Editing Documentation

**DO**:
- ✅ Edit template files in `guides/templates/*.md.in`
- ✅ Edit SQL files in `test/sql/docs_examples/*.sql`
- ✅ Run `make docs` after changes

**DON'T**:
- ❌ Edit generated files in `guides/*.md`
- ❌ Copy SQL code directly into templates
- ❌ Commit without rebuilding docs

## Workflow

### Adding a New Example

1. **Create SQL file**:
   ```bash
   vim test/sql/docs_examples/01_quickstart_new_example.sql
   ```

2. **Add SQL code**:
   ```sql
   -- Example: [Description]
   -- Demonstrates: [Concept]

   SELECT * FROM TS_FORECAST(...);
   ```

3. **Reference in template**:
   Edit `guides/templates/01_quickstart.md.in`:
   ```markdown
   ## New Section

   Here's how to do something:

   <!-- include: test/sql/docs_examples/01_quickstart_new_example.sql -->
   ```

4. **Build and verify**:
   ```bash
   make docs
   less guides/01_quickstart.md  # Verify SQL was embedded
   ```

5. **Test**:
   ```bash
   make test-docs  # Ensure SQL runs correctly
   ```

### Updating Existing Documentation

1. **Find the template**:
   ```bash
   # If editing guides/30_basic_forecasting.md
   vim guides/templates/30_basic_forecasting.md.in
   ```

2. **Make changes**:
   - Update text directly in the template
   - For SQL changes, edit the referenced `.sql` file

3. **Rebuild**:
   ```bash
   make docs
   ```

4. **Review**:
   ```bash
   git diff guides/30_basic_forecasting.md
   ```

### Creating a New Guide

1. **Create template**:
   ```bash
   vim guides/templates/60_new_guide.md.in
   ```

2. **Add content with SQL includes**:
   ```markdown
   # New Guide Title

   ## Introduction

   [Text content...]

   ## Example

   <!-- include: test/sql/docs_examples/60_new_guide_example_01.sql -->
   ```

3. **Create SQL examples**:
   ```bash
   vim test/sql/docs_examples/60_new_guide_example_01.sql
   ```

4. **Build and test**:
   ```bash
   make docs
   make test-docs
   ```

5. **Update index**:
   Edit `guides/templates/99_guide_index.md.in` to add your new guide.

## Template Syntax

### Include Directive

Basic syntax:
```markdown
<!-- include: test/sql/docs_examples/example.sql -->
```

This will be replaced with:
````markdown
```sql
[Contents of example.sql]
```
````

### Best Practices

1. **Use descriptive filenames**:
   ```
   ✅ 01_quickstart_create_sample_data_02.sql
   ❌ example1.sql
   ```

2. **Keep SQL examples focused**:
   ```sql
   -- ✅ One concept per file
   -- Example: Basic forecasting with AutoETS
   SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                             {'seasonal_period': 7});
   ```

3. **Add comments to SQL**:
   ```sql
   -- Example: Complete forecasting workflow
   -- Demonstrates: Data prep → Forecast → Evaluation

   -- Step 1: Prepare data
   CREATE TABLE sales_clean AS ...

   -- Step 2: Generate forecast
   SELECT * FROM TS_FORECAST(...);
   ```

4. **Use consistent naming**:
   - `{guide}_{category}_{number}.sql`
   - Categories: forecast, evaluate, data_quality, statistics, etc.

## Build System

### Build Process

The build script (`scripts/build_docs.sh`):

1. Finds all `.md.in` templates
2. For each template:
   - Reads line by line
   - When it finds `<!-- include: path/to/file.sql -->`:
     - Reads the SQL file
     - Wraps it in ` ```sql ... ``` `
     - Inserts into output
3. Generates final `.md` files in `guides/`
4. Runs markdownlint on generated files

### Testing System

The test script (`scripts/test_sql_examples.sh`):

1. Finds all `.sql` files in `test/sql/docs_examples/`
2. For each file:
   - Creates a temporary test file
   - Adds extension loading code
   - Appends the SQL example
   - Runs it with DuckDB
   - Reports success/failure

### Git Hooks

The pre-commit hook (installed via `make install-hooks`):

1. Builds documentation: `scripts/build_docs.sh`
2. Lints markdown: `markdownlint guides/**/*.md`
3. Tests SQL examples: `scripts/test_sql_examples.sh`
4. Auto-stages generated `.md` files
5. Fails commit if any step fails

Skip with: `git commit --no-verify`

## Troubleshooting

### "File not found" warning during build

```
⚠️  Warning: File not found: test/sql/docs_examples/missing.sql
```

**Solution**: Create the missing SQL file or fix the path in the template.

### SQL example test fails

```
❌ 01_quickstart_forecast_03.sql FAILED
```

**Solution**:
1. Test the SQL manually: `duckdb :memory: < test/sql/docs_examples/01_quickstart_forecast_03.sql`
2. Fix syntax errors or missing dependencies
3. Rebuild: `make docs`

### Markdown lint errors

```
❌ Markdownlint found issues
```

**Solution**:
```bash
# Auto-fix many issues
markdownlint --fix 'guides/**/*.md'

# Or adjust .markdownlint.json to disable rules
```

### Generated file has wrong content

**Check**:
1. Is the template correct? `guides/templates/*.md.in`
2. Is the SQL file correct? `test/sql/docs_examples/*.sql`
3. Did you rebuild? `make docs`

### Build exits with error code 1

Common causes:
- markdownlint not installed: `npm install -g markdownlint-cli`
- Missing SQL files referenced in templates
- Syntax errors in SQL files

## Advanced Usage

### Conditional Includes

Not currently supported, but you can work around it:

```markdown
<!-- For version 1.0 -->
<!-- include: test/sql/docs_examples/v1_example.sql -->

<!-- For version 2.0, use: v2_example.sql instead -->
```

### Shared Examples

Reuse SQL examples across multiple guides:

```markdown
In guides/templates/quickstart.md.in:
<!-- include: test/sql/docs_examples/shared_basic_forecast.sql -->

In guides/templates/tutorial.md.in:
This same example applies here:
<!-- include: test/sql/docs_examples/shared_basic_forecast.sql -->
```

### Organizing Large Guides

For guides with many examples, use subdirectories:

```
test/sql/docs_examples/
├── api_reference/
│   ├── forecasting/
│   │   ├── 01_basic.sql
│   │   └── 02_advanced.sql
│   └── metrics/
│       ├── 01_mae.sql
│       └── 02_rmse.sql
```

Reference as:
```markdown
<!-- include: test/sql/docs_examples/api_reference/forecasting/01_basic.sql -->
```

## Style Guide

### SQL Code Style

```sql
-- ✅ Good: Clear, commented, formatted
-- Example: Forecasting with confidence intervals
-- Demonstrates: Setting confidence_level parameter

SELECT
    forecast_step,
    point_forecast,
    lower,
    upper
FROM TS_FORECAST(
    'sales',
    date,
    amount,
    'AutoETS',
    28,
    {'seasonal_period': 7, 'confidence_level': 0.95}
)
WHERE forecast_step <= 7
ORDER BY forecast_step;
```

```sql
-- ❌ Bad: Unclear, no comments, cramped
SELECT * FROM TS_FORECAST('sales',date,amount,'AutoETS',28,{'seasonal_period':7});
```

### Markdown Style

- Use `##` for major sections
- Use `###` for subsections
- Code blocks: use ` ```sql ` for SQL, ` ```bash ` for shell
- Lists: `-` for unordered, `1.` for ordered
- Emphasis: `**bold**` for important, `*italic*` for emphasis

### Documentation Tone

- **Be clear**: Explain what and why
- **Be concise**: Get to the point
- **Be helpful**: Provide examples
- **Be accurate**: Test all examples

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Documentation

on: [push, pull_request]

jobs:
  docs:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Install markdownlint
        run: npm install -g markdownlint-cli

      - name: Build documentation
        run: make docs

      - name: Test SQL examples
        run: make test-docs

      - name: Check for uncommitted changes
        run: |
          git diff --exit-code guides/
          # Fail if generated docs weren't committed
```

## Maintenance Checklist

Weekly:
- [ ] Review and merge documentation PRs
- [ ] Check for broken links
- [ ] Update examples with new features

Monthly:
- [ ] Review all guides for accuracy
- [ ] Update version numbers
- [ ] Check performance benchmarks

Release:
- [ ] Update all version references
- [ ] Rebuild all documentation
- [ ] Test all SQL examples
- [ ] Update changelog

## Resources

- **Build script**: `scripts/build_docs.sh`
- **Test script**: `scripts/test_sql_examples.sh`
- **Makefile**: Top-level targets
- **Examples index**: `test/sql/docs_examples/00_README.md`
- **Main README**: `00_README.md`

## Getting Help

If you encounter issues:

1. Check this guide
2. Review `test/sql/docs_examples/00_README.md`
3. Look at existing templates for examples
4. Ask in GitHub discussions

## Summary

**Key Points**:
- ✅ Templates are in `guides/templates/*.md.in`
- ✅ SQL examples are in `test/sql/docs_examples/*.sql`
- ✅ Generated files are in `guides/*.md` (committed)
- ✅ Always rebuild with `make docs`
- ✅ Test with `make test-docs`
- ✅ Use git hooks for automation

**Remember**: The build system ensures SQL examples are always tested and documentation stays in sync!
