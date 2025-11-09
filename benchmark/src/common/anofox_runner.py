"""
Shared Anofox-forecast benchmark runner.

Generic runner for benchmarking Anofox forecast models from DuckDB extension.
"""
import time
from pathlib import Path
from typing import List, Dict, Callable, Optional

import duckdb
import pandas as pd


def run_anofox_benchmark(
    benchmark_name: str,
    train_df: pd.DataFrame,
    horizon: int,
    seasonality: int,
    models_config: List[Dict],
    output_dir: Path,
    group: str = 'Daily',
    extension_path: Optional[Path] = None
):
    """
    Run Anofox benchmarks with specified models.

    Parameters
    ----------
    benchmark_name : str
        Name of the benchmark (e.g., 'baseline', 'ets', 'theta')
    train_df : pd.DataFrame
        Training data with columns [unique_id, ds, y]
    horizon : int
        Forecast horizon
    seasonality : int
        Seasonal period
    models_config : List[Dict]
        List of model configurations, each dict contains:
        - 'name': str - Model name
        - 'params': Callable[[int], Dict] - Function that takes seasonality and returns model parameters
    output_dir : Path
        Directory to save results
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    extension_path : Path, optional
        Path to DuckDB extension. If None, uses default location.

    Returns
    -------
    List[Dict]
        List of result dictionaries containing metrics for each model
    """
    # Prepare data for DuckDB
    print(f"Loaded {len(train_df)} rows from {train_df['unique_id'].nunique()} series")
    print(f"Forecast horizon: {horizon}, Seasonality: {seasonality}")

    # Convert ds column to proper dates
    train_df['ds'] = pd.to_datetime('2020-01-01') + pd.to_timedelta(train_df['ds'].astype(int) - 1, unit='D')
    train_df['ds'] = train_df['ds'].dt.date

    # Find the extension
    if extension_path is None:
        extension_path = Path(__file__).parent.parent.parent.parent / 'build' / 'release' / 'extension' / 'anofox_forecast' / 'anofox_forecast.duckdb_extension'

    if not extension_path.exists():
        print(f"ERROR: Extension not found at {extension_path}")
        print("Please build the extension first with: make release")
        raise FileNotFoundError(f"Extension not found at {extension_path}")

    # Connect to DuckDB and load extension
    con = duckdb.connect(':memory:', config={'allow_unsigned_extensions': 'true'})
    con.execute(f"LOAD '{extension_path}'")
    print(f"Loaded extension from {extension_path}")

    # Create table from data
    con.execute("CREATE TABLE train AS SELECT * FROM train_df")
    print(f"Created table with {con.execute('SELECT COUNT(*) FROM train').fetchone()[0]} rows")

    # Output directory
    output_dir.mkdir(parents=True, exist_ok=True)

    all_results = []

    # Run each model
    for model_config in models_config:
        model_name = model_config['name']
        params_fn = model_config['params']

        print(f"\n{'='*60}")
        print(f"Running {model_name} forecasts...")
        print(f"{'='*60}")
        start_time = time.time()

        # Get model parameters
        params = params_fn(seasonality)

        forecast_query = f"""
            SELECT *
            FROM TS_FORECAST_BY(
                'train',
                unique_id,
                ds,
                y,
                '{model_name}',
                {horizon},
                {params}
            )
        """
        
        try:
            fcst_df = con.execute(forecast_query).fetchdf()
            
            # Rename columns to standardized names
            rename_map = {
                'unique_id': 'id_cols',
                'date': 'time_col',
                'point_forecast': 'forecast_col'
            }
            
            # Handle different prediction interval column names
            if 'lower_90' in fcst_df.columns:
                rename_map['lower_90'] = 'lower'
                rename_map['upper_90'] = 'upper'
            elif 'lower_95' in fcst_df.columns:
                rename_map['lower_95'] = 'lower'
                rename_map['upper_95'] = 'upper'
            
            fcst_df = fcst_df.rename(columns=rename_map)
            
            # Keep only the columns we need
            keep_cols = ['id_cols', 'time_col', 'forecast_col']
            if 'lower' in fcst_df.columns:
                keep_cols.extend(['lower', 'upper'])
            
            fcst_df = fcst_df[keep_cols].sort_values(['id_cols', 'time_col'])
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

            all_results.append({
                'model': model_name,
                'time_seconds': elapsed_time,
                'forecast_points': len(fcst_df),
                'series_count': train_df['unique_id'].nunique()
            })

        except Exception as e:
            print(f"\n❌ Error during {model_name} forecast: {e}")
            import traceback
            traceback.print_exc()
            continue

    con.close()
    print(f"\n{'='*60}")
    print(f"All {benchmark_name} benchmarks completed")
    print(f"{'='*60}")

    return all_results
