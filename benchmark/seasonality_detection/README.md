# Seasonality Detection Benchmark

Benchmark suite for evaluating the seasonality detection methods in the `anofox-forecast` DuckDB extension.

## Overview

This benchmark replicates a simulation study design from FDA (Functional Data Analysis) seasonal analysis literature. It generates synthetic time series with known seasonality characteristics and evaluates how well different detection methods identify seasonality.

## Detection Methods Tested

| Method | SQL Function | Description |
|--------|-------------|-------------|
| FFT Period | `ts_detect_periods(values, 'fft')` | Fast Fourier Transform based |
| ACF Period | `ts_estimate_period_acf(values)` | Autocorrelation function based |
| Variance Strength | `ts_seasonal_strength(values, period, 'variance')` | Variance ratio method |
| Spectral Strength | `ts_seasonal_strength(values, period, 'spectral')` | Spectral density based |
| Wavelet Strength | `ts_seasonal_strength(values, period, 'wavelet')` | Wavelet decomposition based |
| Classification | `ts_classify_seasonality(values, period)` | Multi-criteria classification |
| Change Detection | `ts_detect_seasonality_changes(values, period)` | Regime change detection |

## Simulation Scenarios

1. **Strong Seasonal** - Clear sinusoidal pattern with high signal-to-noise ratio
2. **Weak Seasonal** - Low amplitude pattern with high noise
3. **No Seasonal** - Pure noise with optional trend (null case)
4. **Trending Seasonal** - Seasonality with strong linear trend
5. **Variable Amplitude** - Time-varying amplitude (amplitude modulation)
6. **Emerging Seasonal** - No seasonality transitioning to strong seasonality
7. **Fading Seasonal** - Strong seasonality fading to none

## Requirements

- Python 3.9+
- Quarto 1.3+
- Built `anofox-forecast` extension

### Python Setup

```bash
cd benchmark/seasonality_detection
python -m venv .venv
source .venv/bin/activate  # On Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

## Running the Benchmark

1. Build the extension:
```bash
cd /path/to/forecast-extension
make
```

2. Activate the virtual environment and run the Quarto report:
```bash
cd benchmark/seasonality_detection
source .venv/bin/activate
quarto render seasonality_detection_report.qmd
```

3. View the results in `_output/seasonality_detection_report.html`

## Quick Test

To test the simulation and detection modules:

```bash
cd benchmark/seasonality_detection
source .venv/bin/activate

# Test simulation
python -m src.simulation

# Test detection (requires built extension)
python -m src.detection

# Test evaluation
python -m src.evaluation
```

## Output Files

After running the benchmark:

- `_output/seasonality_detection_report.html` - Full HTML report
- `detection_results.csv` - Raw detection results
- `method_metrics.csv` - Method-level performance metrics
- `scenario_metrics.csv` - Scenario-level metrics

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
├── requirements.txt
├── _quarto.yml
├── .gitignore
├── seasonality_detection_report.qmd
└── src/
    ├── __init__.py
    ├── simulation.py    # Time series generation
    ├── detection.py     # Detection method wrappers
    └── evaluation.py    # Metrics computation
```
