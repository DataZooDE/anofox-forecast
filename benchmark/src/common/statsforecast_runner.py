"""
Generic statsforecast benchmark runner.

Provides a unified interface for running statsforecast models across different benchmarks,
eliminating code duplication while maintaining flexibility for model-specific configurations.
"""
import time
import sys
from pathlib import Path
from typing import List, Dict, Optional, Callable

import pandas as pd
from statsforecast import StatsForecast


def run_statsforecast_benchmark(
    benchmark_name: str,
    train_df: pd.DataFrame,
    horizon: int,
    freq: str,
    seasonality: int,
    models_config: List[Dict],
    output_dir: Path,
    group: str = 'Daily',
    include_prediction_intervals: bool = True,
    column_mapping: Optional[Dict[str, str]] = None,
):
    """
    Run statsforecast benchmark with given models and configuration.

    Parameters
    ----------
    benchmark_name : str
        Name of the benchmark (e.g., 'Baseline', 'ETS', 'Theta', 'MFLES', 'statsforecast')
    train_df : pd.DataFrame
        Training data with columns: unique_id, ds, y
    horizon : int
        Forecast horizon
    freq : str
        Frequency string for statsforecast (e.g., 'D', 'h', 'W')
    seasonality : int
        Seasonal period
    models_config : List[Dict]
        List of model configurations. Each dict should have:
        - 'model_factory': Callable that returns a statsforecast model instance
        - 'params': Dict of parameters to pass to the model factory (optional)
        - 'display_name': Optional override for the model name in outputs (optional)
    output_dir : Path
        Directory to save results
    group : str
        M4 frequency group name (for file naming)
    include_prediction_intervals : bool
        Whether to include prediction intervals in the forecast
    column_mapping : Optional[Dict[str, str]]
        Optional column name mapping for standardizing output
    """
    print(f"Loaded {len(train_df)} rows from {train_df['unique_id'].nunique()} series")
    print(f"Forecast horizon: {horizon}, Seasonality: {seasonality}")

    # Convert integer indices to actual dates for proper time series modeling
    train_df = train_df.copy()
    if not pd.api.types.is_datetime64_any_dtype(train_df['ds']):
        print(f"Converting integer indices to dates...")
        train_df['ds'] = pd.to_datetime('2020-01-01') + pd.to_timedelta(train_df['ds'].astype(int) - 1, unit='D')
    else:
        print(f"Using existing datetime dates...")

    # Prepare output directory
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"\nRunning statsforecast {benchmark_name} forecasts with {len(models_config)} models...")

    # Run each model separately to get individual timing
    all_forecasts = []
    all_metrics = []
    series_count = train_df['unique_id'].nunique()

    for model_cfg in models_config:
        model_factory = model_cfg['model_factory']
        params = model_cfg.get('params', {})

        # Inject seasonality if model expects it
        if 'season_length' in params or 'seasonality' in str(model_factory):
            if 'season_length' not in params:
                params['season_length'] = seasonality

        model = model_factory(**params)
        model_class_name = type(model).__name__
        
        # Use display_name if provided, otherwise use class name
        model_display_name = model_cfg.get('display_name', model_class_name)

        print(f"\n{'='*60}")
        print(f"Running {model_display_name} ({model_class_name})...")
        print(f"{'='*60}")

        try:
            start_time = time.time()

            sf = StatsForecast(
                models=[model],
                freq=freq,
                n_jobs=-1,  # Use all cores
            )

            # Run forecast with or without prediction intervals
            if include_prediction_intervals:
                fcst_df = sf.forecast(df=train_df, h=horizon, level=[95])
            else:
                fcst_df = sf.forecast(df=train_df, h=horizon)

            elapsed_time = time.time() - start_time

            # Reset index to get unique_id and ds as columns
            fcst_df = fcst_df.reset_index()
            
            # Drop any 'index' column if it exists (can cause merge conflicts)
            if 'index' in fcst_df.columns:
                fcst_df = fcst_df.drop(columns=['index'])

            # Rename model columns to use display name
            if model_class_name != model_display_name:
                rename_map = {}
                for col in fcst_df.columns:
                    if col.startswith(model_class_name):
                        new_col = col.replace(model_class_name, model_display_name, 1)
                        rename_map[col] = new_col
                if rename_map:
                    fcst_df = fcst_df.rename(columns=rename_map)

            print(f"✅ {model_display_name} completed in {elapsed_time:.2f} seconds")
            print(f"Generated {len(fcst_df)} forecast points for {fcst_df['unique_id'].nunique()} series")

            # Store forecast and metrics
            all_forecasts.append(fcst_df)
            all_metrics.append({
                'model': f'statsforecast-{model_display_name}',
                'group': group,
                'time_seconds': elapsed_time,
                'series_count': series_count,
                'forecast_points': len(fcst_df),
            })

        except Exception as e:
            print(f"\n❌ Error during {model_display_name} forecast: {e}")
            import traceback
            traceback.print_exc()
            continue

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

    # Apply column mapping if provided (for standardizing column names)
    if column_mapping:
        merged_fcst = merged_fcst.rename(columns=column_mapping)
        # Keep only mapped columns if they exist
        mapped_cols = list(column_mapping.values())
        existing_cols = [col for col in mapped_cols if col in merged_fcst.columns]
        if existing_cols:
            merged_fcst = merged_fcst[existing_cols]

    # Save merged forecasts
    forecast_file = output_dir / f'statsforecast-{benchmark_name}-{group}.parquet'
    merged_fcst.to_parquet(forecast_file, index=False)
    print(f"\nSaved merged forecasts to {forecast_file}")

    # Save timing metrics
    metrics_file = output_dir / f'statsforecast-{benchmark_name}-{group}-metrics.parquet'
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
