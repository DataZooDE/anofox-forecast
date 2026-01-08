# Seasonality Detection Benchmark

Benchmark suite for evaluating the seasonality detection methods in the `anofox-forecast` DuckDB extension.

## Overview

This benchmark replicates a simulation study design from FDA (Functional Data Analysis) seasonal analysis literature. It generates synthetic time series with known seasonality characteristics and evaluates how well different detection methods identify seasonality.

The report serves as both a **benchmark** and a **tutorial**, demonstrating how to use the extension's SQL functions for seasonality detection.

## Detection Methods Tested

| Method | SQL Function | Description |
|--------|-------------|-------------|
| Basic Detection | `ts_detect_seasonality(values)` | Returns array of detected periods |
| Full Analysis | `ts_analyze_seasonality(values)` | Comprehensive struct with strength metrics |
| FFT Period | `ts_estimate_period_fft(values)` | Fast Fourier Transform based |
| ACF Period | `ts_estimate_period_acf(values)` | Autocorrelation function based |
| Multi-Method | `ts_detect_periods(values, method)` | Detection using specified method |
| Multiple Periods | `ts_detect_multiple_periods(values)` | Detect multiple seasonal components |

## Simulation Scenarios

1. **Strong Seasonal** - Clear sinusoidal pattern with high signal-to-noise ratio
2. **Weak Seasonal** - Low amplitude pattern with high noise
3. **No Seasonal** - Pure noise with optional trend (null case)
4. **Trending Seasonal** - Seasonality with strong linear trend
5. **Variable Amplitude** - Time-varying amplitude (amplitude modulation)
6. **Emerging Seasonal** - No seasonality transitioning to strong seasonality
7. **Fading Seasonal** - Strong seasonality fading to none

## Requirements

- R 4.0+
- Quarto 1.3+
- Built `anofox-forecast` extension

### R Package Dependencies

The following R packages are required:

```r
install.packages(c("DBI", "duckdb", "ggplot2", "dplyr", "tidyr", "purrr", "knitr", "scales"))
```

## Running the Benchmark

1. **Build the extension** (from project root):
```bash
make
```

2. **Render the Quarto report**:
```bash
cd benchmark/seasonality_detection
quarto render seasonality_detection_report.qmd
```

3. **View the results** in `_output/seasonality_detection_report.html`

## Quick SQL Examples

After loading the extension in DuckDB:

```sql
-- Load extension
SET allow_unsigned_extensions = true;
LOAD 'path/to/anofox_forecast.duckdb_extension';

-- Basic detection
SELECT ts_detect_seasonality(values) FROM my_timeseries;

-- Full analysis with strength metrics
SELECT
    (ts_analyze_seasonality(values)).primary_period,
    (ts_analyze_seasonality(values)).seasonal_strength
FROM my_timeseries;

-- FFT-based period estimation
SELECT
    (ts_estimate_period_fft(values)).period,
    (ts_estimate_period_fft(values)).confidence
FROM my_timeseries;
```

## Output Files

After running the benchmark:

- `_output/seasonality_detection_report.html` - Full HTML report with visualizations
- `_output/seasonality_detection_report.pdf` - PDF version (if PDF rendering enabled)

## Customizing the Benchmark

Edit `seasonality_detection_report.qmd` to modify:

- `N_SERIES` - Number of series per scenario (default: 100, use 500 for full study)
- `PERIOD` - True seasonal period (default: 12)
- `N_POINTS` - Length of each series (default: 120)
- `SEED` - Random seed for reproducibility

## Directory Structure

```
seasonality_detection/
├── README.md
├── _quarto.yml
├── .gitignore
└── seasonality_detection_report.qmd
```

## Report Structure

The generated report includes:

1. **Executive Summary** - Quick overview of method recommendations
2. **Introduction** - Overview of detection methods
3. **Setup** - R and DuckDB configuration
4. **Data Simulation** - Generation of test time series
5. **Detection Methods Tutorial** - SQL examples for each method
6. **Evaluation** - Performance metrics and visualizations
7. **Recommendations** - Method selection guide
8. **Appendix** - SQL function reference
