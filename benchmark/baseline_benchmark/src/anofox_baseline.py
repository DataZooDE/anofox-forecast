"""
Anofox-forecast baseline models benchmarking script.

Runs baseline forecasting models from the anofox-forecast DuckDB extension and saves results.
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


BASELINE_MODELS = [
    'Naive',
    'SeasonalNaive',
    'RandomWalkWithDrift',
    'SMA',
    'SeasonalWindowAverage',
]


def run_benchmark(group: str = 'Daily', model: str = None):
    """
    Run anofox-forecast baseline benchmarks on M4 dataset.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    model : str, optional
        Specific baseline model to run. If None, runs all models.
        Options: 'Naive', 'SeasonalNaive', 'RandomWalkWithDrift'
    """
    models_to_run = [model] if model else BASELINE_MODELS

    print(f"Loading M4 {group} data...")
    train_df, horizon, freq, seasonality = get_data('data', group, train=True)

    # Prepare data for DuckDB
    print(f"Loaded {len(train_df)} rows from {train_df['unique_id'].nunique()} series")
    print(f"Forecast horizon: {horizon}, Seasonality: {seasonality}")

    # Convert ds column to proper timestamps
    train_df['ds'] = pd.to_datetime('2020-01-01') + pd.to_timedelta(train_df['ds'].astype(int) - 1, unit='D')
    train_df['ds'] = train_df['ds'].dt.date

    # Find the extension
    extension_path = Path(__file__).parent.parent.parent.parent / 'build' / 'release' / 'extension' / 'anofox_forecast' / 'anofox_forecast.duckdb_extension'

    if not extension_path.exists():
        print(f"ERROR: Extension not found at {extension_path}")
        print("Please build the extension first with: make release")
        sys.exit(1)

    # Connect to DuckDB and load extension
    con = duckdb.connect(':memory:', config={'allow_unsigned_extensions': 'true'})
    con.execute(f"LOAD '{extension_path}'")
    print(f"Loaded extension from {extension_path}")

    # Create table from data
    con.execute("CREATE TABLE train AS SELECT * FROM train_df")
    print(f"Created table with {con.execute('SELECT COUNT(*) FROM train').fetchone()[0]} rows")

    # Output directory
    output_dir = Path('baseline_benchmark/results')
    output_dir.mkdir(parents=True, exist_ok=True)

    # Run each model
    for model_name in models_to_run:
        print(f"\n{'='*60}")
        print(f"Running {model_name} forecasts...")
        print(f"{'='*60}")
        start_time = time.time()

        # Build forecast query with appropriate parameters
        params = {'seasonal_period': seasonality}

        forecast_query = f"""
            SELECT
                unique_id AS id_cols,
                date_col AS time_col,
                point_forecast AS forecast_col,
                lower,
                upper
            FROM TS_FORECAST_BY(
                'train',
                unique_id,
                ds,
                y,
                '{model_name}',
                {horizon},
                {params}
            )
            ORDER BY id_cols, time_col
        """

        try:
            fcst_df = con.execute(forecast_query).fetchdf()
            elapsed_time = time.time() - start_time

            print(f"\n✅ {model_name} completed in {elapsed_time:.2f} seconds")
            print(f"Generated {len(fcst_df)} forecast points for {fcst_df['id_cols'].nunique()} series")

            # Save forecasts
            forecast_file = output_dir / f'anofox-{model_name}-{group}.parquet'
            fcst_df.to_parquet(forecast_file, index=False)
            print(f"Saved forecasts to {forecast_file}")

            # Save timing metrics
            metrics_file = output_dir / f'anofox-{model_name}-{group}-metrics.parquet'
            metrics_df = pd.DataFrame({
                'model': [f'anofox-{model_name}'],
                'group': [group],
                'time_seconds': [elapsed_time],
                'series_count': [train_df['unique_id'].nunique()],
                'forecast_points': [len(fcst_df)],
            })
            metrics_df.to_parquet(metrics_file, index=False)
            print(f"Saved metrics to {metrics_file}")

        except Exception as e:
            print(f"\n❌ Error during {model_name} forecast: {e}")
            import traceback
            traceback.print_exc()
            continue

    con.close()
    print(f"\n{'='*60}")
    print("All baseline benchmarks completed")
    print(f"{'='*60}")


if __name__ == '__main__':
    fire.Fire(run_benchmark)
