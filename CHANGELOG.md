# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

Releases from `2026.09.26` onward use CalVer (`vYYYY.MM.DD`, the date the binary set
was cut), matching the rest of the anofox and erpl extensions. Earlier entries below
use the semver numbering this project started with.

## [2026.09.26] - 2026-09-26

### Changed

- Every public function now documents itself in `duckdb_functions()`: 285 of 285
  eligible entries carry a description, a runnable example and categories. An agent
  connected to a DuckDB database can only learn what an extension does by querying that
  view — a README is not reachable from a SQL connection.
- The 36 `_`-prefixed natives behind the public `ts_*` surface are declared as
  documentation exemptions rather than given prose. They are implementation detail with
  a documented public counterpart each (`_ts_forecast_native` behind `ts_forecast`), and
  anofox-evolve's prompt-vocabulary builder already skips them for the same reason.
- First release to use CalVer; see the note above.

## [0.4.14] - 2026-06-17

### Changed
- Build and ship against both DuckDB **v1.4.5 LTS** and **v1.5.4** (latest). The distribution
  pipeline now runs two build jobs (`duckdb-lts-build` and `duckdb-latest-build`) instead of a
  single v1.5.x build.
- Updated `extension-ci-tools` to the maintained line branches `v1.4-andium` (LTS) and
  `v1.5-variegata` (latest); `duckdb` submodule moved to v1.5.4.

### Fixed
- Linux build against DuckDB v1.4.5 LTS: pass `-Wl,--allow-multiple-definition` so GNU ld on
  glibc/GCC-14 tolerates DuckDB's byte-identical constexpr static members (`LogicalType::FLOAT`,
  etc.) that the v1.4 line emits into multiple static archives. No-op on the v1.5 line.

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
