"""
Shared Anofox-forecast benchmark runner.

Generic runner for benchmarking Anofox forecast models from DuckDB extension.
"""
import time
import sys
import os
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
    extension_path: Optional[Path] = None,
    use_community_extension: bool = True
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
        # Check environment variable first
        env_path = os.environ.get("ANOFOX_EXTENSION_PATH")
        if env_path:
            extension_path = Path(env_path)
        else:
            # Fallback to local build path
            extension_path = Path(__file__).parent.parent.parent.parent / 'build' / 'extension' / 'anofox_forecast' / 'anofox_forecast.duckdb_extension'

    if not extension_path.exists() and not use_community_extension:
        print(f"WARNING: Extension not found at {extension_path}")
        if os.environ.get("ANOFOX_EXTENSION_PATH"):
             print("The path was provided via ANOFOX_EXTENSION_PATH environment variable.")
        print("Attempting to use community extension as fallback if available, or build it locally.")
        # We raise error if strictly not using community extension, but if the intent is to support docker without build, 
        # we might want to be softer here or assume if it's missing we fail unless community is allowed.
        # The existing logic raises FileNotFoundError.
        
        # If we are in Docker and expect the extension, this should fail.
        raise FileNotFoundError(f"Extension not found at {extension_path}")

    # Connect to DuckDB and load extension
    con = duckdb.connect(':memory:', config={'allow_unsigned_extensions': 'true'})
    if use_community_extension:
        con.execute("FORCE INSTALL anofox_forecast FROM community;")
        con.execute("LOAD 'anofox_forecast';")
    else:
        con.execute(f"LOAD '{extension_path}'")
    print(f"Loaded extension from {extension_path}")

    # Create table from data
    con.execute("CREATE TABLE train AS SELECT * FROM train_df")
    print(f"Created table with {con.execute('SELECT COUNT(*) FROM train').fetchone()[0]} rows")

    # Output directory
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"\nRunning anofox {benchmark_name} forecasts with {len(models_config)} models...")

    # Run each model separately to get individual timing
    all_forecasts = []
    all_metrics = []
    series_count = train_df['unique_id'].nunique()

    for model_config in models_config:
        model_name = model_config['name']
        params_fn = model_config['params']

        print(f"\n{'='*60}")
        print(f"Running {model_name}...")
        print(f"{'='*60}")

        try:
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
            
            fcst_df = con.execute(forecast_query).fetchdf()
            
            # Rename columns to standardized names (matching statsforecast format)
            rename_map = {
                'date': 'ds',
                'point_forecast': model_name
            }
            
            # Handle different prediction interval column names
            if 'lower_90' in fcst_df.columns:
                rename_map['lower_90'] = f'{model_name}-lo-95'
                rename_map['upper_90'] = f'{model_name}-hi-95'
            elif 'lower_95' in fcst_df.columns:
                rename_map['lower_95'] = f'{model_name}-lo-95'
                rename_map['upper_95'] = f'{model_name}-hi-95'
            
            fcst_df = fcst_df.rename(columns=rename_map)
            
            # Keep only the columns we need for merging
            keep_cols = ['unique_id', 'ds', model_name]
            for col in [f'{model_name}-lo-95', f'{model_name}-hi-95']:
                if col in fcst_df.columns:
                    keep_cols.append(col)
            
            fcst_df = fcst_df[keep_cols].sort_values(['unique_id', 'ds'])
            
            elapsed_time = time.time() - start_time

            print(f"✅ {model_name} completed in {elapsed_time:.2f} seconds")
            print(f"Generated {len(fcst_df)} forecast points for {fcst_df['unique_id'].nunique()} series")

            # Store forecast and metrics
            all_forecasts.append(fcst_df)
            all_metrics.append({
                'model': f'anofox-{model_name}',
                'group': group,
                'time_seconds': elapsed_time,
                'series_count': series_count,
                'forecast_points': len(fcst_df),
            })

        except Exception as e:
            print(f"\n❌ Error during {model_name} forecast: {e}")
            import traceback
            traceback.print_exc()
            continue

    con.close()

    if not all_forecasts:
        print(f"\n❌ No forecasts were generated successfully")
        sys.exit(1)

    # Merge all forecasts
    print(f"\n{'='*60}")
    print("Merging forecasts from all models...")
    print(f"{'='*60}")

    # Start with the first forecast (has unique_id and ds)
    merged_fcst = all_forecasts[0]

    # Merge remaining forecasts (add their model columns)
    for fcst_df in all_forecasts[1:]:
        # Get model columns (exclude unique_id and ds)
        model_cols = [col for col in fcst_df.columns if col not in ['unique_id', 'ds']]
        # Merge on unique_id and ds
        merged_fcst = merged_fcst.merge(
            fcst_df[['unique_id', 'ds'] + model_cols],
            on=['unique_id', 'ds'],
            how='outer'
        )

    print(f"Merged forecast shape: {merged_fcst.shape}")
    print(f"Columns: {list(merged_fcst.columns)}")

    # Save merged forecasts
    forecast_file = output_dir / f'anofox-{benchmark_name}-{group}.parquet'
    merged_fcst.to_parquet(forecast_file, index=False)
    print(f"\nSaved merged forecasts to {forecast_file}")

    # Save timing metrics
    metrics_file = output_dir / f'anofox-{benchmark_name}-{group}-metrics.parquet'
    metrics_df = pd.DataFrame(all_metrics)
    metrics_df.to_parquet(metrics_file, index=False)
    print(f"Saved per-model metrics to {metrics_file}")

    # Print summary
    print(f"\n{'='*60}")
    print("Timing Summary:")
    print(f"{'='*60}")
    for metric in all_metrics:
        print(f"  {metric['model']}: {metric['time_seconds']:.2f}s")
    total_time = sum(m['time_seconds'] for m in all_metrics)
    print(f"  Total: {total_time:.2f}s")
    print(f"{'='*60}")

    return all_metrics
