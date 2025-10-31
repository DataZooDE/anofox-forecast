"""
Statsforecast (Nixtla) AutoARIMA benchmarking script.

Runs AutoARIMA from statsforecast library and saves results.
"""
import time
import sys
from pathlib import Path

import fire
import pandas as pd
from statsforecast import StatsForecast
from statsforecast.models import AutoARIMA

# Add parent directory to path to import data module
sys.path.insert(0, str(Path(__file__).parent.parent))
from src.data import get_data


def run_benchmark(group: str = 'Daily'):
    """
    Run statsforecast AutoARIMA benchmark on M4 dataset.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"Loading M4 {group} data...")
    train_df, horizon, freq, seasonality = get_data('data', group, train=True)

    print(f"Loaded {len(train_df)} rows from {train_df['unique_id'].nunique()} series")
    print(f"Forecast horizon: {horizon}, Seasonality: {seasonality}")

    # Configure AutoARIMA model
    models = [AutoARIMA(season_length=seasonality)]

    # Initialize StatsForecast
    sf = StatsForecast(
        df=train_df,
        models=models,
        freq=freq,
        n_jobs=-1,  # Use all available cores
    )

    # Start benchmarking
    print(f"\nRunning AutoARIMA forecasts...")
    start_time = time.time()

    try:
        fcst_df = sf.forecast(h=horizon, level=[95])
        elapsed_time = time.time() - start_time

        print(f"\n✅ Forecast completed in {elapsed_time:.2f} seconds")
        print(f"Generated forecasts for {len(fcst_df)} series")

        # Prepare output - statsforecast returns wide format, convert to long
        fcst_df = fcst_df.reset_index()

        # Rename columns to match expected format
        fcst_df = fcst_df.rename(columns={
            'AutoARIMA': 'statsforecast',
            'AutoARIMA-lo-95': 'lower',
            'AutoARIMA-hi-95': 'upper',
        })

        # Save forecasts
        output_dir = Path('arima_benchmark/results')
        output_dir.mkdir(parents=True, exist_ok=True)

        forecast_file = output_dir / f'statsforecast-{group}.csv'
        fcst_df.to_csv(forecast_file, index=False)
        print(f"Saved forecasts to {forecast_file}")

        # Save timing metrics
        metrics_file = output_dir / f'statsforecast-{group}-metrics.csv'
        metrics_df = pd.DataFrame({
            'model': ['statsforecast'],
            'group': [group],
            'time_seconds': [elapsed_time],
            'series_count': [train_df['unique_id'].nunique()],
            'forecast_points': [len(fcst_df) * horizon],
        })
        metrics_df.to_csv(metrics_file, index=False)
        print(f"Saved metrics to {metrics_file}")

    except Exception as e:
        print(f"\n❌ Error during forecast: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    fire.Fire(run_benchmark)
