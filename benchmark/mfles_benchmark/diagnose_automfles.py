"""
Diagnose AutoMFLES parameter selection to understand why it's performing worse
"""

import duckdb
import pandas as pd
import numpy as np
from pathlib import Path

def diagnose_worst_series():
    """Check what parameters AutoMFLES selected for worst-performing series"""

    # Load M4 Daily data
    data_path = Path(__file__).parent / "data" / "m4" / "datasets" / "Daily-train.csv"
    df = pd.read_csv(data_path, index_col=0)

    # Focus on just 2 series to see detailed logging
    test_series = ['D1', 'D100']  # Just test 2 series

    # Connect to DuckDB and load extension
    con = duckdb.connect(config={'allow_unsigned_extensions': 'true'})

    try:
        extension_path = '/home/simonm/projects/duckdb/anofox-forecast/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'
        con.execute(f"INSTALL '{extension_path}'")
        con.execute("LOAD anofox_forecast")
    except Exception as e:
        print(f"Extension error: {e}")
        return

    print("=" * 80)
    print("AutoMFLES Parameter Selection Diagnosis")
    print("=" * 80)

    for series_id in test_series:
        # Get series data
        series = df.loc[series_id].dropna().values

        # Create temp table
        con.execute("DROP TABLE IF EXISTS test_series")
        series_df = pd.DataFrame({
            'unique_id': [series_id] * len(series),
            'ds': pd.to_datetime('2020-01-01') + pd.to_timedelta(range(len(series)), unit='D'),
            'y': series
        })
        series_df['ds'] = series_df['ds'].dt.date
        con.execute("CREATE TABLE test_series AS SELECT * FROM series_df")

        # Run both MFLES and AutoMFLES
        mfles_result = con.execute("""
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

        automfles_result = con.execute("""
            SELECT point_forecast
            FROM TS_FORECAST_BY(
                'test_series',
                unique_id,
                ds,
                y,
                'AutoMFLES',
                14,
                {'seasonal_periods': [7]}
            )
            ORDER BY date_col
        """).fetchdf()

        mfles_fc = mfles_result['point_forecast'].values
        automfles_fc = automfles_result['point_forecast'].values

        print(f"\n{series_id}:")
        print(f"  Series length: {len(series)}")
        print(f"  Mean: {np.mean(series):.2f}, Std: {np.std(series):.2f}")
        print(f"  Last 7 values: {series[-7:]}")
        print(f"  MFLES forecasts: {mfles_fc}")
        print(f"  AutoMFLES forecasts: {automfles_fc}")
        print(f"  Difference (Auto - MFLES): {automfles_fc - mfles_fc}")

    con.close()

if __name__ == "__main__":
    diagnose_worst_series()
