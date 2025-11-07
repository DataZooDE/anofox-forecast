---
name: duckdb-extension-development
description: Use this agent for DuckDB extension development tasks including documentation, testing, build system configuration, and code examples. Automatically invoked for DuckDB extension projects.
tools: Read, Edit, Bash, Write, Glob, Grep
model: inherit
---

# DuckDB Extension Development Guidelines

## Markdown standards
- Always run markdownlint on any markdown files created or edited
- Install using: `npx markdownlint-cli`
- Fix all linting issues before the task


## Testing preferences
- Use descriptive function names starting with `test_`
- Prefer fixtures over setup/teardown methods


## Testing apporach
- Never create throwaway test scripts or ad hoc verification files
- If you need to test functionality, write a proper test in the test suite
- All tests go in the `tests/` directory following the project structure
- Tests should be runnable with the rest of the suite
- Even for quick verification, write it as a real test that provides ongoing value


## Build System
- Always use Ninja as the build system for compilation
- Configure CMake with `-G Ninja` flag when generating build files
- Run builds using `ninja` command instead of `make`


## Documentation & Code Examples

- **Single Source of Truth for Examples**: All code examples (SQL, code snippets, etc.) should be stored in separate, executable files under `test/sql/` directory, not embedded directly in markdown files
- **Examples as Tests**: Code examples are part of the test suite in `test/sql/`, ensuring they are validated with every test run
- **Template-Based Documentation**: Use `.md.in` template files in `guides/templates/` with include directives (e.g., `<!-- include: test/sql/example.sql -->`) instead of hardcoded examples in documentation
- **Separated Source and Output**: Keep templates in `guides/templates/` and generate output to `guides/` for clear separation of concerns
- **Automated Testing**: All code examples must be automatically testable to ensure they remain functional and up-to-date
- **Build Process**: Documentation files (`.md`) in `guides/` are generated artifacts built from templates in `guides/templates/` during the build process
- **Pre-commit Validation**: Use git hooks to automatically build documentation and test examples before commits to catch breaking changes early
- **Markdown Linting**: Always run markdownlint on any markdown files created or edited to ensure consistent formatting and style
- **Commit Generated Files**: Generated `.md` files in `guides/` are committed to the repository so they are visible to GitHub visitors and in online tutorials
- **Descriptive Naming**: Example files should use descriptive, snake_case names that clearly indicate their purpose (e.g., `basic_query.sql`, `advanced_join_01.sql`)
- **Version Control Both**: Both templates (`guides/templates/*.md.in`) and generated documentation (`guides/*.md`) are version-controlled; templates are the source of truth, generated files provide GitHub visibility
- **Self-Documenting Examples**: Code examples should be complete, runnable pieces that demonstrate actual usage patterns, not pseudocode
- **Automated Workflow**: The build-lint-test-commit cycle is automated via hooks, ensuring documentation is always in sync with examples and properly formatted
- **Organized Structure**: Documentation lives in `guides/` with a clear distinction between source templates and generated output
- **Update**: Update the API documentation each time the API changes
