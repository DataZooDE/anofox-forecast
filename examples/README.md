# Forecasting Model Validation Suite

This directory contains comprehensive validation tests that compare anofox-time implementations with the statsforecast Python package.

## Quick Start

```bash
cd /home/simonm/projects/ai/anofox-forecast/examples
```

## Air Passenger


### Python

#### Live Editing

```bash
uv run marimo run airpassenger_benchmark.py
```

#### Export to HTML

```bash
uv run marimo export airpassenger_example.py -o output/airpassenger_example.html
```


## Test Files

### Comprehensive Validation
- **`comprehensive_ets_validation.py`** ⭐ **MAIN TEST SUITE**
  - Tests all ETS/AutoETS model combinations
  - Compares with statsforecast Python package
  - Uses AirPassengers dataset (132 observations)
  - Automatic pass/fail criteria (<5% error)

### Original Tests
- **`airpassenger_benchmark.py`** - Original AutoETS comparison
- **`m5.sql`** - M5 dataset test

### Prerequisites
The uv environment is already set up in this directory with required packages:
- statsforecast
- pandas
- numpy
- duckdb

**Last Updated**: October 24, 2025  

