# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.4] - 2026-01-03

### Changed
- Reimplemented core algorithms in Rust for improved performance and maintainability
- C++ FFI layer provides seamless DuckDB integration
- Full API compatibility with previous versions

### Added
- SeasonalWindowAverage forecasting model (32 models total)
- RandomWalkWithDrift alias for backward compatibility
- PostHog telemetry integration (opt-out via `DATAZOO_DISABLE_TELEMETRY=1`)
- Native `ts_fill_forward_operator` with improved parallel execution safety
- MSTL `insufficient_data` parameter with 'fail', 'trend', 'none' modes
- Feature parameter validation warnings

### Fixed
- Timestamp boundary alignment in gap filling macros
- Thread safety in table operators via `MaxThreads()` override

## [0.2.3] - Previous C++ Release

See [cpp-legacy branch](https://github.com/DataZooDE/anofox-forecast/tree/cpp-legacy) for previous C++ implementation history.
