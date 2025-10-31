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

### Testing

- Add tests for new features
- Ensure all existing tests pass
- Test SQL examples work correctly
- Verify documentation builds without errors

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
