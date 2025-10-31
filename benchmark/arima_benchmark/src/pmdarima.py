"""
pmdarima AutoARIMA benchmarking script.

Runs AutoARIMA from pmdarima library and saves results.
"""
import time
import sys
from pathlib import Path
import warnings

import fire
import pandas as pd
import numpy as np
from pmdarima import auto_arima

# Add parent directory to path to import data module
sys.path.insert(0, str(Path(__file__).parent.parent))
from src.data import get_data

warnings.filterwarnings('ignore')


def forecast_series(series_data, horizon, seasonality):
    """
    Forecast a single series using pmdarima auto_arima.

    Parameters
    ----------
    series_data : pd.Series
        Time series data
    horizon : int
        Forecast horizon
    seasonality : int
        Seasonal period

    Returns
    -------
    dict
        Forecast point predictions and confidence intervals
    """
    try:
        model = auto_arima(
            series_data,
            seasonal=True,
            m=seasonality,
            suppress_warnings=True,
            error_action='ignore',
            stepwise=True,
            n_jobs=1,
        )
        forecast, conf_int = model.predict(n_periods=horizon, return_conf_int=True, alpha=0.05)

        return {
            'forecast': forecast,
            'lower': conf_int[:, 0],
            'upper': conf_int[:, 1],
        }
    except Exception as e:
        # Return NaN if model fails
        return {
            'forecast': np.full(horizon, np.nan),
            'lower': np.full(horizon, np.nan),
            'upper': np.full(horizon, np.nan),
        }


def run_benchmark(group: str = 'Daily'):
    """
    Run pmdarima AutoARIMA benchmark on M4 dataset.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"Loading M4 {group} data...")
    train_df, horizon, freq, seasonality = get_data('data', group, train=True)

    print(f"Loaded {len(train_df)} rows from {train_df['unique_id'].nunique()} series")
    print(f"Forecast horizon: {horizon}, Seasonality: {seasonality}")

    # Start benchmarking
    print(f"\nRunning AutoARIMA forecasts...")
    start_time = time.time()

    all_forecasts = []
    unique_ids = train_df['unique_id'].unique()

    try:
        for i, uid in enumerate(unique_ids, 1):
            if i % 100 == 0:
                elapsed = time.time() - start_time
                print(f"  Processed {i}/{len(unique_ids)} series ({elapsed:.1f}s)...")

            series_data = train_df[train_df['unique_id'] == uid]['y'].values
            result = forecast_series(series_data, horizon, seasonality)

            for h in range(horizon):
                all_forecasts.append({
                    'unique_id': uid,
                    'forecast_step': h + 1,
                    'pmdarima': result['forecast'][h],
                    'lower': result['lower'][h],
                    'upper': result['upper'][h],
                })

        elapsed_time = time.time() - start_time

        print(f"\n✅ Forecast completed in {elapsed_time:.2f} seconds")

        # Create forecast dataframe
        fcst_df = pd.DataFrame(all_forecasts)
        print(f"Generated {len(fcst_df)} forecast points for {fcst_df['unique_id'].nunique()} series")

        # Save forecasts
        output_dir = Path('arima_benchmark/results')
        output_dir.mkdir(parents=True, exist_ok=True)

        forecast_file = output_dir / f'pmdarima-{group}.csv'
        fcst_df.to_csv(forecast_file, index=False)
        print(f"Saved forecasts to {forecast_file}")

        # Save timing metrics
        metrics_file = output_dir / f'pmdarima-{group}-metrics.csv'
        metrics_df = pd.DataFrame({
            'model': ['pmdarima'],
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


if __name__ == '__main__':
    fire.Fire(run_benchmark)
