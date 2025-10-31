"""
Anofox-forecast AutoARIMA benchmarking script.

Runs AutoARIMA from the anofox-forecast DuckDB extension and saves results.
"""
import time
import sys
from pathlib import Path

import fire
import duckdb
import pandas as pd

# Add parent directory to path to import data module
sys.path.insert(0, str(Path(__file__).parent.parent))
from src.data import get_data


def run_benchmark(group: str = 'Daily'):
    """
    Run anofox-forecast AutoARIMA benchmark on M4 dataset.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"Loading M4 {group} data...")
    train_df, horizon, freq, seasonality = get_data('data', group, train=True)

    # Prepare data for DuckDB
    print(f"Loaded {len(train_df)} rows from {train_df['unique_id'].nunique()} series")
    print(f"Forecast horizon: {horizon}, Seasonality: {seasonality}")

    # Find the extension
    extension_path = Path(__file__).parent.parent.parent.parent / 'build' / 'release' / 'extension' / 'anofox_forecast' / 'anofox_forecast.duckdb_extension'

    if not extension_path.exists():
        print(f"ERROR: Extension not found at {extension_path}")
        print("Please build the extension first with: make release")
        sys.exit(1)

    # Connect to DuckDB and load extension
    con = duckdb.connect(':memory:')
    con.execute(f"LOAD '{extension_path}'")
    print(f"Loaded extension from {extension_path}")

    # Create table from data
    con.execute("CREATE TABLE train AS SELECT * FROM train_df")
    print(f"Created table with {con.execute('SELECT COUNT(*) FROM train').fetchone()[0]} rows")

    # Start benchmarking
    print(f"\nRunning AutoARIMA forecasts...")
    start_time = time.time()

    # Run forecasts using TS_FORECAST_BY
    forecast_query = f"""
        SELECT
            unique_id,
            date_col AS ds,
            point_forecast AS AutoARIMA,
            lower,
            upper,
            forecast_step
        FROM TS_FORECAST_BY(
            'train',
            'unique_id',
            'ds',
            'y',
            'AutoARIMA',
            {horizon},
            {{'seasonal_period': {seasonality}, 'confidence_level': 0.95}}
        )
        ORDER BY unique_id, forecast_step
    """

    try:
        fcst_df = con.execute(forecast_query).fetchdf()
        elapsed_time = time.time() - start_time

        print(f"\n✅ Forecast completed in {elapsed_time:.2f} seconds")
        print(f"Generated {len(fcst_df)} forecast points for {fcst_df['unique_id'].nunique()} series")

        # Save forecasts
        output_dir = Path('arima_benchmark/results')
        output_dir.mkdir(parents=True, exist_ok=True)

        forecast_file = output_dir / f'anofox-{group}.csv'
        fcst_df.to_csv(forecast_file, index=False)
        print(f"Saved forecasts to {forecast_file}")

        # Save timing metrics
        metrics_file = output_dir / f'anofox-{group}-metrics.csv'
        metrics_df = pd.DataFrame({
            'model': ['anofox'],
            'group': [group],
            'time_seconds': [elapsed_time],
            'series_count': [train_df['unique_id'].nunique()],
            'forecast_points': [len(fcst_df)],
        })
        metrics_df.to_csv(metrics_file, index=False)
        print(f"Saved metrics to {metrics_file}")

    except Exception as e:
        print(f"\n❌ Error during forecast: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

    finally:
        con.close()


if __name__ == '__main__':
    fire.Fire(run_benchmark)
