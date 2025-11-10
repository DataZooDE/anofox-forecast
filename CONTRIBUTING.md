# Contributing to Anofox Forecast

Thank you for your interest in contributing to Anofox Forecast! This document provides guidelines for contributing to the project.

## Getting Started

1. Fork the repository
2. Clone your fork with submodules:
   ```bash
   git clone --recurse-submodules https://github.com/<your-username>/anofox-forecast.git
   cd anofox-forecast
   ```
3. Build the project:
   ```bash
   make -j$(nproc)
   ```

## Development Workflow

### Making Changes

1. Create a new branch for your feature or bugfix:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. Make your changes following the coding standards

3. Build and test your changes:
   ```bash
   make clean
   make -j$(nproc)
   make test
   ```

4. If you're modifying documentation:
   ```bash
   # Edit template files in guides/templates/*.md.in
   make docs
   make test-docs
   ```

5. Run code quality checks before committing:
   ```bash
   ./scripts/check_code_quality.sh
   ```

### Code Quality

Before submitting your changes, ensure they pass all code quality checks:

#### Format Checking

The project uses automated code formatters:
- **C/C++ files**: `clang-format` version 11.0.1
- **Python files**: `black` version 24+
- **CMake files**: `cmake-format`

**Check formatting locally:**
```bash
./scripts/check_code_quality.sh
```

**Auto-fix formatting issues:**
```bash
./scripts/check_code_quality.sh --fix
# Or use make:
make format-fix
```

**Install formatting tools:**
```bash
pip install "black>=24" clang_format==11.0.1 cmake-format
```

These same checks run in GitHub Actions CI, so running them locally before pushing will catch issues early.

### Documentation

- **Guide templates**: Edit files in `guides/templates/*.md.in`
- **SQL examples**: Edit files in `test/sql/docs_examples/`
- **Build docs**: Run `make docs` to generate final documentation
- **Test examples**: Run `make test-docs` to validate SQL examples

See [Documentation Build System](README.md#documentation-build-system) for details.

### Code Style

- Follow existing code formatting and style
- Use meaningful variable and function names
- Add comments for complex logic
- Write clear commit messages

### Forecasting Model Contributions

When adding new forecasting models:

1. **Core Library Integration**:
   - Implement model in `anofox-time` C++ library
   - Follow existing model interfaces (fit/predict pattern)
   - Ensure parameter validation in model constructor
   - Add unit tests in `anofox-time/tests/`

2. **Extension Integration**:
   - Register model in `src/model_factory.cpp`
   - Add parameter parsing in model factory
   - Ensure model name is case-insensitive
   - Update model list in documentation

3. **Parameter Specification**:
   - Document all parameters in `docs/PARAMETERS.md`
   - Specify types (INTEGER, DOUBLE, BOOLEAN, INTEGER[])
   - Document ranges and validation rules
   - Include behavioral notes and use cases

4. **Testing Requirements**:
   - Basic forecast test in `test/sql/models/`
   - Parameter validation tests
   - Edge case tests (minimum data, seasonality requirements)
   - Output schema validation

5. **Documentation Updates**:
   - Add to model list in README.md
   - Add to parameter guide (docs/PARAMETERS.md)
   - Add to appropriate guide templates in `guides/templates/`
   - Include usage examples in relevant guides

### Testing

#### Test Requirements

- **New forecasting models**: Add tests in `test/sql/` covering:
  - Basic functionality with default parameters
  - Parameter validation (ranges, types, required parameters)
  - Edge cases (minimum data points, seasonality requirements)
  - Output schema validation
- **New metrics**: Test with:
  - Equal-length arrays
  - Edge cases (zeros, negative values, empty arrays)
  - GROUP BY with LIST() aggregation
- **Data preparation macros**: Test with:
  - All supported date types (INTEGER, DATE, TIMESTAMP)
  - Empty series, single-point series
  - Edge cases specific to the macro
- **Integration tests**: Ensure all tests pass via `make test`
- **SQL examples**: All examples in guides must be executable and pass `make test-docs`
- **Documentation**: Verify builds without errors via `make docs`

## Submitting Changes

1. Commit your changes with clear, descriptive messages:
   ```bash
   git add .
   git commit -m "Add feature: description of your changes"
   ```

2. Push to your fork:
   ```bash
   git push origin feature/your-feature-name
   ```

3. Create a Pull Request:
   - Go to the original repository
   - Click "New Pull Request"
   - Select your branch
   - Provide a clear description of your changes
   - Reference any related issues

### Pull Request Guidelines

- **Title**: Clear, concise description of changes
- **Description**: Explain what, why, and how
- **Tests**: Ensure all tests pass
- **Documentation**: Update guides if needed
- **Breaking Changes**: Clearly mark and document

## Reporting Issues

### Bug Reports

Include:
- DuckDB version
- Extension version
- Operating system
- Minimal reproducible example
- Expected vs actual behavior
- Error messages (if any)

### Feature Requests

Include:
- Use case description
- Proposed API/interface
- Example usage
- Alternatives considered

## Code of Conduct

- Be respectful and inclusive
- Welcome newcomers
- Focus on constructive feedback
- Assume good intentions

## License

By contributing, you agree that your contributions will be licensed under the project's Business Source License 1.1 (BSL 1.1).

## Questions?

- **Documentation**: [guides/](guides/)
- **Issues**: [GitHub Issues](https://github.com/DataZooDE/anofox-forecast/issues)
- **Discussions**: [GitHub Discussions](https://github.com/DataZooDE/anofox-forecast/discussions)
- **Email**: support@anofox.com

## Recognition

Contributors will be acknowledged in release notes and documentation. Thank you for helping make Anofox Forecast better!
