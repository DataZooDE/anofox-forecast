"""
Quick test to verify trend_penalty fix on worst-performing series
"""

import duckdb
import pandas as pd
import numpy as np
from pathlib import Path

def test_worst_series():
    """Test MFLES on the 3 worst-performing series after the fix."""

    # Worst series identified from previous analysis
    worst_series = ['D2191', 'D2168', 'D2139']

    # Load M4 Daily data
    data_path = Path(__file__).parent / "data" / "m4" / "datasets" / "Daily-train.csv"
    df = pd.read_csv(data_path, index_col=0)

    # Connect to DuckDB and load extension
    con = duckdb.connect(config={'allow_unsigned_extensions': 'true'})

    try:
        # Try to install from local path
        extension_path = '/home/simonm/projects/duckdb/anofox-forecast/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'
        print(f"Installing extension from: {extension_path}")
        con.execute(f"INSTALL '{extension_path}'")
        print("Extension installed successfully")
    except Exception as e:
        print(f"Install error: {e}")
        print("Trying alternative approach...")

    try:
        print("Loading extension...")
        con.execute("LOAD anofox_forecast")
        print("Extension loaded successfully")
    except Exception as e:
        print(f"Load error: {e}")
        raise

    print("\n" + "="*80)
    print("Testing MFLES with trend_penalty=false on Worst Series")
    print("="*80)

    for series_id in worst_series:
        # Get series data
        series = df.loc[series_id].dropna().values

        # Create temp table with proper format
        con.execute("DROP TABLE IF EXISTS test_series")
        series_df = pd.DataFrame({
            'unique_id': [series_id] * len(series),
            'ds': pd.to_datetime('2020-01-01') + pd.to_timedelta(range(len(series)), unit='D'),
            'y': series
        })
        series_df['ds'] = series_df['ds'].dt.date
        con.execute("CREATE TABLE test_series AS SELECT * FROM series_df")

        # Run MFLES using TS_FORECAST_BY
        result = con.execute("""
            SELECT point_forecast
            FROM TS_FORECAST_BY(
                'test_series',
                unique_id,
                ds,
                y,
                'MFLES',
                14,
                {'seasonal_periods': [7]}
            )
            ORDER BY date_col
        """).fetchdf()

        forecasts = result['point_forecast'].values

        print(f"\n{series_id}:")
        print(f"  Series length: {len(series)}")
        print(f"  Last 5 values: {series[-5:]}")
        print(f"  Mean: {np.mean(series):.2f}")
        print(f"  Trend (% per obs): {((series[-1] - series[0]) / len(series) / np.mean(series)) * 100:.4f}%")
        print(f"  Forecasts (14-step):")
        for i, fc in enumerate(forecasts, 1):
            print(f"    h={i}: {fc:.2f}")
        print(f"  Mean forecast: {np.mean(forecasts):.2f}")
        print(f"  Forecast range: {min(forecasts):.2f} - {max(forecasts):.2f}")

    con.close()

    print("\n" + "="*80)
    print("Expected: Forecasts should continue the upward trend")
    print("(Not be dampened by R²-based penalty)")
    print("="*80)

if __name__ == "__main__":
    test_worst_series()
