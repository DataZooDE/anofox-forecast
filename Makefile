PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=anofox_forecast
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Skip building jemalloc (not needed for this extension)
EXT_FLAGS=-DSKIP_EXTENSIONS="jemalloc"

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# Documentation targets
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