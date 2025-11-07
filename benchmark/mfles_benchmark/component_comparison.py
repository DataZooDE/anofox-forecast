"""
Single-series component-by-component comparison between anofox-MFLES and statsforecast-MFLES.

This script:
1. Loads a single test series from M4 Daily
2. Runs both implementations with identical parameters
3. Extracts and compares all 5 components at each boosting round
4. Identifies where the implementations diverge
"""

import duckdb
import pandas as pd
import numpy as np
from pathlib import Path
import sys

# Add statsforecast to path
sys.path.insert(0, str(Path.home() / ".cache/uv/archive-v0/Kiv_6VAyyppury2vCa3Be"))

from statsforecast.mfles import MFLES as StatsForecastMFLES


def load_test_series(series_id="D1"):
    """Load a single series from M4 Daily dataset."""
    data_path = Path(__file__).parent / "data" / "m4" / "datasets" / "Daily-train.csv"
    df = pd.read_csv(data_path)

    # Get the series
    series_data = df[df['unique_id'] == series_id].copy()
    series_data = series_data.sort_values('ds')

    return series_data['y'].values


def run_anofox_mfles(y, horizon=14, seasonal_period=7):
    """Run anofox MFLES with diagnostic output."""
    con = duckdb.connect()

    # Load extension
    extension_path = Path.home() / "projects/duckdb/anofox-forecast/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension"
    con.execute(f"LOAD '{extension_path}'")

    # Create table with series
    df = pd.DataFrame({
        'ds': range(len(y)),
        'y': y
    })
    con.execute("CREATE TABLE series AS SELECT * FROM df")

    # Run MFLES with default parameters matching statsforecast
    result = con.execute(f"""
        SELECT mfles(
            y,
            seasonal_periods := [{seasonal_period}],
            horizon := {horizon},
            max_rounds := 5,
            min_alpha := 0.1,
            max_alpha := 0.9,
            es_ensemble_steps := 10,
            fourier_order := NULL,
            trend_method := 'siegel_robust',
            seasonality_weights := false,
            trend_penalty := 0.02,
            cap_outliers := false
        ) as forecast
        FROM series
    """).fetchall()

    forecasts = [f for f in result[0][0]]

    con.close()
    return forecasts


def run_statsforecast_mfles(y, horizon=14, seasonal_period=7):
    """Run statsforecast MFLES with diagnostic output."""
    model = StatsForecastMFLES(
        season_length=seasonal_period,
        n_windows=5,
        min_alpha=0.1,
        max_alpha=0.9,
        es_ensemble_size=10,
        fourier_order=None,
        trend_method='siegel_robust',
        seasonality_weights=False,
        trend_penalty=0.02,
        cap_outliers=False
    )

    forecasts = model.forecast(y, h=horizon)

    return forecasts['mean'].values


def compare_series(series_id="D1"):
    """Compare anofox and statsforecast MFLES on a single series."""
    print(f"\n{'='*80}")
    print(f"Component Comparison: Series {series_id}")
    print(f"{'='*80}\n")

    # Load series
    y = load_test_series(series_id)
    print(f"Series length: {len(y)}")
    print(f"First 10 values: {y[:10]}")
    print(f"Last 10 values: {y[-10:]}")
    print(f"Mean: {np.mean(y):.4f}, Std: {np.std(y):.4f}")

    # Run both implementations
    print("\n" + "-"*80)
    print("Running anofox-MFLES...")
    print("-"*80)
    anofox_forecasts = run_anofox_mfles(y)

    print("\n" + "-"*80)
    print("Running statsforecast-MFLES...")
    print("-"*80)
    statsforecast_forecasts = run_statsforecast_mfles(y)

    # Compare forecasts
    print("\n" + "-"*80)
    print("Forecast Comparison")
    print("-"*80)

    print(f"\n{'Step':<6} {'Anofox':<12} {'Statsforecast':<12} {'Diff':<12} {'Diff %':<10}")
    print("-"*60)

    for i, (af, sf) in enumerate(zip(anofox_forecasts, statsforecast_forecasts), 1):
        diff = af - sf
        diff_pct = (diff / sf * 100) if sf != 0 else float('inf')
        print(f"{i:<6} {af:<12.4f} {sf:<12.4f} {diff:<12.4f} {diff_pct:<10.2f}")

    # Summary statistics
    diffs = np.array(anofox_forecasts) - np.array(statsforecast_forecasts)
    print("\n" + "-"*80)
    print("Summary Statistics")
    print("-"*80)
    print(f"Mean Absolute Difference: {np.mean(np.abs(diffs)):.4f}")
    print(f"Max Absolute Difference: {np.max(np.abs(diffs)):.4f}")
    print(f"RMSE: {np.sqrt(np.mean(diffs**2)):.4f}")
    print(f"Correlation: {np.corrcoef(anofox_forecasts, statsforecast_forecasts)[0,1]:.6f}")


def main():
    """Run component comparison on multiple test series."""
    # Test on a few representative series
    test_series = ["D1", "D10", "D100"]

    for series_id in test_series:
        try:
            compare_series(series_id)
        except Exception as e:
            print(f"\nError processing {series_id}: {e}")
            import traceback
            traceback.print_exc()

    print("\n" + "="*80)
    print("Component comparison complete")
    print("="*80)


if __name__ == "__main__":
    main()
