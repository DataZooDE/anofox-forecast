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
    print(f"Converting integer indices to dates...")
    train_df = train_df.copy()
    train_df['ds'] = pd.to_datetime('2020-01-01') + pd.to_timedelta(train_df['ds'].astype(int) - 1, unit='D')

    # Instantiate models from configuration
    models = []
    for model_cfg in models_config:
        model_factory = model_cfg['model_factory']
        params = model_cfg.get('params', {})

        # Inject seasonality if model expects it
        if 'season_length' in params or 'seasonality' in str(model_factory):
            if 'season_length' not in params:
                params['season_length'] = seasonality

        model = model_factory(**params)
        models.append(model)

    print(f"\nRunning statsforecast {benchmark_name} forecasts with {len(models)} models...")
    print(f"Models: {[type(m).__name__ for m in models]}")

    # Start benchmarking
    start_time = time.time()

    try:
        sf = StatsForecast(
            models=models,
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

        # Apply column mapping if provided (for standardizing column names)
        if column_mapping:
            fcst_df = fcst_df.rename(columns=column_mapping)
            # Keep only mapped columns if they exist
            mapped_cols = list(column_mapping.values())
            existing_cols = [col for col in mapped_cols if col in fcst_df.columns]
            if existing_cols:
                fcst_df = fcst_df[existing_cols]

        print(f"\n✅ Forecasts completed in {elapsed_time:.2f} seconds")
        print(f"Generated forecasts for {fcst_df['unique_id'].nunique() if 'unique_id' in fcst_df.columns else len(fcst_df)} series")
        print(f"Columns: {list(fcst_df.columns)}")

        # Save results
        output_dir.mkdir(parents=True, exist_ok=True)

        # Save forecasts
        forecast_file = output_dir / f'statsforecast-{benchmark_name}-{group}.parquet'
        fcst_df.to_parquet(forecast_file, index=False)
        print(f"Saved forecasts to {forecast_file}")

        # Save timing metrics
        metrics_file = output_dir / f'statsforecast-{benchmark_name}-{group}-metrics.parquet'

        # Get unique_id column name (might be mapped to id_cols)
        unique_id_col = 'id_cols' if 'id_cols' in train_df.columns else 'unique_id'
        series_count = train_df[unique_id_col].nunique() if unique_id_col in train_df.columns else train_df['unique_id'].nunique()

        metrics_df = pd.DataFrame({
            'model': [f'statsforecast-{type(m).__name__}' for m in models],
            'group': [group] * len(models),
            'time_seconds': [elapsed_time / len(models)] * len(models),  # Split time equally
            'series_count': [series_count] * len(models),
            'forecast_points': [len(fcst_df) // len(models)] * len(models),
        })
        metrics_df.to_parquet(metrics_file, index=False)
        print(f"Saved metrics to {metrics_file}")

    except Exception as e:
        print(f"\n❌ Error during forecast: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
