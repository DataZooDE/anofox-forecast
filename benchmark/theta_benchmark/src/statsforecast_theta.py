"""
Statsforecast Theta methods benchmarking script.

Runs Theta variants from statsforecast and saves results.
"""
import time
import sys
from pathlib import Path

import fire
import pandas as pd
from statsforecast import StatsForecast
from statsforecast.models import AutoTheta, Theta, OptimizedTheta, DynamicTheta, DynamicOptimizedTheta

# Add parent directory to path to import data module
sys.path.insert(0, str(Path(__file__).parent.parent))
from src.data import get_data


def run_benchmark(group: str = 'Daily'):
    """
    Run statsforecast Theta benchmarks on M4 dataset.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"Loading M4 {group} data...")
    train_df, horizon, freq, seasonality = get_data('data', group, train=True)

    print(f"Loaded {len(train_df)} rows from {train_df['unique_id'].nunique()} series")
    print(f"Forecast horizon: {horizon}, Seasonality: {seasonality}")

    # Convert integer indices to actual dates for proper time series modeling
    print(f"Converting integer indices to dates...")
    train_df['ds'] = pd.to_datetime('2020-01-01') + pd.to_timedelta(train_df['ds'].astype(int) - 1, unit='D')

    # Prepare models with seasonality
    models = [
        AutoTheta(season_length=seasonality),
        Theta(season_length=seasonality),
        OptimizedTheta(season_length=seasonality),
        DynamicTheta(season_length=seasonality),
        DynamicOptimizedTheta(season_length=seasonality),
    ]

    print(f"\nRunning statsforecast Theta forecasts with {len(models)} models...")
    print(f"Models: {[type(m).__name__ for m in models]}")

    # Start benchmarking
    start_time = time.time()

    try:
        sf = StatsForecast(
            models=models,
            freq=freq,
            n_jobs=-1,  # Use all cores
        )

        fcst_df = sf.forecast(df=train_df, h=horizon, level=[95])
        elapsed_time = time.time() - start_time

        # Reset index to get unique_id and ds as columns
        fcst_df = fcst_df.reset_index()

        print(f"\n✅ Forecasts completed in {elapsed_time:.2f} seconds")
        print(f"Generated forecasts for {fcst_df['unique_id'].nunique()} series")
        print(f"Columns: {list(fcst_df.columns)}")

        # Save results
        output_dir = Path('theta_benchmark/results')
        output_dir.mkdir(parents=True, exist_ok=True)

        # Save forecasts
        forecast_file = output_dir / f'statsforecast-Theta-{group}.parquet'
        fcst_df.to_parquet(forecast_file, index=False)
        print(f"Saved forecasts to {forecast_file}")

        # Save timing metrics
        metrics_file = output_dir / f'statsforecast-Theta-{group}-metrics.parquet'
        metrics_df = pd.DataFrame({
            'model': [f'statsforecast-{type(m).__name__}' for m in models],
            'group': [group] * len(models),
            'time_seconds': [elapsed_time / len(models)] * len(models),  # Split time equally
            'series_count': [train_df['unique_id'].nunique()] * len(models),
            'forecast_points': [len(fcst_df) // len(models)] * len(models),
        })
        metrics_df.to_parquet(metrics_file, index=False)
        print(f"Saved metrics to {metrics_file}")

    except Exception as e:
        print(f"\n❌ Error during forecast: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    fire.Fire(run_benchmark)
