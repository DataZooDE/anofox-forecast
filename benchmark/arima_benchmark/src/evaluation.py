"""
Evaluation script for ARIMA benchmark.

Computes accuracy metrics (MASE, MAE, RMSE) for all implementations.
"""
import sys
from pathlib import Path

import fire
import pandas as pd
import numpy as np
from tabulate import tabulate

# Add parent directory to path to import data module
sys.path.insert(0, str(Path(__file__).parent.parent))
from src.data import get_data


def mase(y_true, y_pred, y_train, seasonality=1):
    """
    Calculate Mean Absolute Scaled Error (MASE).

    Parameters
    ----------
    y_true : array-like
        Actual values
    y_pred : array-like
        Predicted values
    y_train : array-like
        Training data for scaling
    seasonality : int
        Seasonal period for naive forecast

    Returns
    -------
    float
        MASE value
    """
    y_true = np.asarray(y_true)
    y_pred = np.asarray(y_pred)
    y_train = np.asarray(y_train)

    # Calculate MAE of forecast
    mae_forecast = np.mean(np.abs(y_true - y_pred))

    # Calculate MAE of seasonal naive forecast on training data
    mae_naive = np.mean(np.abs(y_train[seasonality:] - y_train[:-seasonality]))

    # Avoid division by zero
    if mae_naive == 0:
        return np.inf

    return mae_forecast / mae_naive


def evaluate_model(model_name, group):
    """
    Evaluate a single model's forecasts against test data.

    Parameters
    ----------
    model_name : str
        Name of the model (anofox, statsforecast, pmdarima, prophet)
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'

    Returns
    -------
    dict
        Evaluation metrics
    """
    # Load test data
    test_df, horizon, freq, seasonality = get_data('data', group, train=False)
    train_df, *_ = get_data('data', group, train=True)

    # Load forecasts (now using parquet format with standardized columns)
    forecast_file = Path(f'arima_benchmark/results/{model_name}-{group}.parquet')

    if not forecast_file.exists():
        print(f"⚠️  Forecast file not found: {forecast_file}")
        return None

    fcst_df = pd.read_parquet(forecast_file)

    # Rename test data columns to match standardized format
    test_df = test_df.rename(columns={
        'unique_id': 'id_cols',
        'ds': 'date_col',
        'y': 'y_true'
    })

    # Convert test data date_col to match forecast data format
    # M4 data uses integer indices, but forecasts use actual dates
    test_df['date_col'] = pd.to_datetime('2020-01-01') + pd.to_timedelta(test_df['date_col'].astype(int) - 1, unit='D')

    # Merge forecasts with test data using standardized column names
    merged = test_df.merge(fcst_df, on=['id_cols', 'date_col'], how='inner')

    if len(merged) == 0:
        print(f"⚠️  No matching data found after merge for {model_name}")
        return None

    y_true = merged['y_true'].values
    y_pred = merged['forecast_col'].values

    # Calculate metrics
    mae = np.mean(np.abs(y_true - y_pred))
    rmse = np.sqrt(np.mean((y_true - y_pred) ** 2))

    # Calculate MASE for each series and average
    # Rename train_df columns for consistency
    train_df = train_df.rename(columns={'unique_id': 'id_cols'})

    # Pre-group training data by series for efficient lookup
    train_grouped = {uid: group['y'].values for uid, group in train_df.groupby('id_cols')}

    mase_scores = []
    for uid in merged['id_cols'].unique():
        uid_test = merged[merged['id_cols'] == uid]
        uid_train = train_grouped.get(uid, np.array([]))

        if len(uid_train) == 0:
            continue

        uid_y_true = uid_test['y_true'].values
        uid_y_pred = uid_test['forecast_col'].values

        series_mase = mase(uid_y_true, uid_y_pred, uid_train, seasonality)
        if not np.isinf(series_mase):
            mase_scores.append(series_mase)

    avg_mase = np.mean(mase_scores)

    # Load timing metrics (now using parquet)
    metrics_file = Path(f'arima_benchmark/results/{model_name}-{group}-metrics.parquet')
    if metrics_file.exists():
        timing_df = pd.read_parquet(metrics_file)
        time_seconds = timing_df['time_seconds'].values[0]
    else:
        time_seconds = np.nan

    return {
        'model': model_name,
        'group': group,
        'MASE': avg_mase,
        'MAE': mae,
        'RMSE': rmse,
        'time_seconds': time_seconds,
        'series_count': merged['id_cols'].nunique(),
    }


def evaluate_all(group: str = 'Daily'):
    """
    Evaluate all models for a given dataset group.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"\n{'=' * 80}")
    print(f"Evaluating ARIMA Benchmark Results - M4 {group}")
    print(f"{'=' * 80}\n")

    models = ['anofox', 'statsforecast', 'pmdarima']
    results = []

    for model in models:
        print(f"Evaluating {model}...")
        result = evaluate_model(model, group)
        if result:
            results.append(result)

    if not results:
        print("❌ No results to evaluate!")
        return

    # Create results dataframe
    results_df = pd.DataFrame(results)

    # Sort by MASE (lower is better)
    results_df = results_df.sort_values('MASE')

    # Format table
    table_data = []
    for _, row in results_df.iterrows():
        table_data.append([
            row['model'],
            f"{row['MASE']:.3f}",
            f"{row['MAE']:.2f}",
            f"{row['RMSE']:.2f}",
            f"{row['time_seconds']:.2f}s",
            int(row['series_count']),
        ])

    headers = ['Model', 'MASE', 'MAE', 'RMSE', 'Time', 'Series']
    print(tabulate(table_data, headers=headers, tablefmt='grid'))

    # Save results as parquet
    output_file = Path(f'arima_benchmark/results/evaluation-{group}.parquet')
    results_df.to_parquet(output_file, index=False)
    print(f"\nResults saved to {output_file}")


if __name__ == '__main__':
    fire.Fire(evaluate_all)
