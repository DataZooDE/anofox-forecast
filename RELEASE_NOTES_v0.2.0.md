# Release Notes - v0.2.0

**Release Date:** 2025-01-XX  
**DuckDB Version Required:** ≥ v1.4.2

## 🎉 Overview

This release introduces a unified API naming convention, comprehensive time series feature extraction, enhanced data quality analysis, and significant documentation improvements. All changes are **100% backward compatible** through function aliases.

## ✨ Major Features

### 1. Unified API Naming Convention

All functions now follow a consistent naming pattern across all Anofox extensions:
- **Forecast Extension**: `anofox_fcst_ts_*` prefix
- **Backward Compatible**: All functions available as aliases (e.g., `ts_forecast` still works)
- **208 files updated** with new naming convention

**Example:**
```sql
-- New naming (recommended)
SELECT * FROM anofox_fcst_ts_forecast('sales', date, amount, 'AutoETS', 7, {'seasonal_period': 7});

-- Old naming (still works)
SELECT * FROM ts_forecast('sales', date, amount, 'AutoETS', 7, {'seasonal_period': 7});
```

### 2. Time Series Feature Extraction (76+ Features)

Comprehensive feature extraction compatible with tsfresh:
- **76+ statistical features** for ML pipelines
- **tsfresh-compatible** feature vectors
- **Flexible configuration** via JSON/CSV configs
- **Native parallelization** with GROUP BY support
- **Window function support** for rolling feature extraction

**New Functions:**
- `anofox_fcst_ts_features` - Extract features from time series
- `anofox_fcst_ts_features_list` - List available features
- `anofox_fcst_ts_features_config_from_json` - Load config from JSON
- `anofox_fcst_ts_features_config_from_csv` - Load config from CSV

### 3. Data Quality Health Card Framework

Enhanced data quality analysis with comprehensive health reporting:
- **Data Quality Health Card** with detailed metrics
- **Frequency parameter** support for `TS_STATS` and `TS_DATA_QUALITY`
- **New KPI**: `n_duplicate_timestamps` tracking
- **Improved error handling** and validation

**New Functions:**
- `anofox_fcst_ts_data_quality` - Comprehensive data quality analysis
- `anofox_fcst_ts_data_quality_summary` - Summary statistics

### 4. Safe Mode / Error Resilience

Forecasting functions now include error resilience:
- **Graceful error handling** for invalid inputs
- **Validation improvements** in `TS_FORECAST_AGG`
- **Enhanced error messages** for debugging

## 📚 Documentation Improvements

### Comprehensive API Reference
- **Complete API reference** (`docs/API_REFERENCE.md`) with detailed function documentation
- **Top-down workflow** organization
- **Function descriptions** with examples
- **Parameter reference** with defaults

### Guide Restructuring
- **Restructured EDA guide** aligned with API reference
- **Enhanced quickstart guide** with single/multiple series examples
- **Improved evaluation metrics guide** with complete train/test examples
- **Time series features guide** with comprehensive examples
- **Multi-language guide** updates for Python, R, Julia integration

### Example Improvements
- **Copy-paste ready** SQL examples with sample datasets
- **Complete train/test data** in evaluation examples
- **Multiple model comparison** examples

## 🐛 Bug Fixes

### Data Preparation
- Fixed `ts_fill_nulls_interpolate` parameter substitution issues
- Fixed `ts_drop_gappy` group column handling
- Added integer-based frequency support via function overloading for `TS_FILL_GAPS` and `TS_FILL_FORWARD`

### Feature Extraction
- Fixed LZ complexity segfault
- Fixed skewness, energy_ratio_by_chunks, and max_langevin_fixed_point features
- Fixed fourier_entropy to match scipy welch PSD calculation
- Fixed ts_features segfault in window functions

### Forecasting
- Fixed SMA rolling forecast behavior
- Fixed MFLES horizon validation and fitted values computation
- Fixed batch consistency tests (join on forecast_step)
- Fixed intermittency threshold (use >= 50.0 instead of > 50.0)

### Data Quality
- Fixed validation error handling in `TS_FORECAST_AGG`
- Fixed SQL syntax: flattened nested WITH clauses in data quality macros
- Fixed MSVC build: split large SQL string into concatenated literals

## 🧪 Testing & Quality

### Test Coverage
- **150/154 tests passing** (97.4% pass rate)
- **New alias test suite** with 24 assertions covering all function types
- **Comprehensive test coverage** for anofox-time library
- **Enhanced error resilience tests** for forecasting functions

### Code Quality
- **Code quality checks** (format and tidy) in CI workflow
- **Unit tests** integrated into main distribution pipeline
- **Code coverage documentation** added to README

## 🚀 Infrastructure & CI/CD

### GitHub Actions
- **Code quality checks** (format and tidy) automated
- **Unit tests** for anofox-time library
- **Benchmark deployment** workflow for AWS
- **Parallel job execution** for faster CI

### Benchmark Infrastructure
- **Docker infrastructure** for benchmarks
- **AWS deployment** with Terraform and Fargate
- **M5 dataset** benchmark suite
- **M4 dataset** benchmark refactoring

### Build System
- **Eigen3 integration** via vcpkg
- **License documentation** for third-party dependencies
- **Improved dependency management**

## 📊 Statistics

- **128 commits** since v0.1.1
- **208 files changed** in unified naming PR
- **2,097 insertions, 1,560 deletions** in unified naming PR
- **177+ SQL test files** updated
- **11 guide template files** updated

## 🔄 Migration Guide

### For Existing Users

**No action required!** All existing code continues to work:
- All `ts_*` function names remain available as aliases
- Both naming conventions produce identical results
- Zero breaking changes

### Recommended Migration (Optional)

For new code, we recommend using the new naming convention:
```sql
-- Old (still works)
SELECT * FROM ts_forecast(...);

-- New (recommended)
SELECT * FROM anofox_fcst_ts_forecast(...);
```

## 📦 Installation

```sql
-- Community Extension
INSTALL anofox_forecast FROM community;
LOAD anofox_forecast;
```

Or build from source:
```bash
git clone --recurse-submodules https://github.com/DataZooDE/anofox-forecast.git
cd anofox-forecast
make release
```

## 🙏 Acknowledgments

Special thanks to all contributors and the DuckDB team for making this release possible!

## 📝 Full Changelog

For a complete list of changes, see the [git log](https://github.com/DataZooDE/anofox-forecast/compare/v0.1.1...v0.2.0).

---

**Previous Release:** [v0.1.1](https://github.com/DataZooDE/anofox-forecast/releases/tag/v0.1.1)  
**Next Release:** TBD


