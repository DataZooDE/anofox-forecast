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

    # Get last date for each series to generate forecast dates
    last_dates = train_df.groupby('unique_id')['ds'].max().to_dict()

    try:
        for i, uid in enumerate(unique_ids, 1):
            if i % 100 == 0:
                elapsed = time.time() - start_time
                print(f"  Processed {i}/{len(unique_ids)} series ({elapsed:.1f}s)...")

            series_data = train_df[train_df['unique_id'] == uid]['y'].values
            result = forecast_series(series_data, horizon, seasonality)

            # Get last date for this series and generate forecast dates
            last_date = last_dates[uid]

            for h in range(horizon):
                # Generate forecast date based on frequency
                if freq == 'D':
                    forecast_date = pd.Timestamp(last_date) + pd.Timedelta(days=h+1)
                elif freq == 'h':
                    forecast_date = pd.Timestamp(last_date) + pd.Timedelta(hours=h+1)
                elif freq == 'W':
                    forecast_date = pd.Timestamp(last_date) + pd.Timedelta(weeks=h+1)
                else:
                    forecast_date = pd.Timestamp(last_date) + pd.Timedelta(days=h+1)

                all_forecasts.append({
                    'id_cols': uid,
                    'date_col': forecast_date,
                    'forecast_col': result['forecast'][h],
                    'lower': result['lower'][h],
                    'upper': result['upper'][h],
                })

        elapsed_time = time.time() - start_time

        print(f"\n✅ Forecast completed in {elapsed_time:.2f} seconds")

        # Create forecast dataframe
        fcst_df = pd.DataFrame(all_forecasts)
        print(f"Generated {len(fcst_df)} forecast points for {fcst_df['id_cols'].nunique()} series")

        # Save forecasts
        output_dir = Path('arima_benchmark/results')
        output_dir.mkdir(parents=True, exist_ok=True)

        forecast_file = output_dir / f'pmdarima-{group}.parquet'
        fcst_df.to_parquet(forecast_file, index=False)
        print(f"Saved forecasts to {forecast_file}")

        # Save timing metrics
        metrics_file = output_dir / f'pmdarima-{group}-metrics.parquet'
        metrics_df = pd.DataFrame({
            'model': ['pmdarima'],
            'group': [group],
            'time_seconds': [elapsed_time],
            'series_count': [train_df['unique_id'].nunique()],
            'forecast_points': [len(fcst_df)],
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
