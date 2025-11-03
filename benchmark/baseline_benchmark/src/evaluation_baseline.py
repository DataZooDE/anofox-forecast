"""
Baseline models benchmarking evaluation script.

Evaluates baseline model forecasts against actual test data using MASE, MAE, and RMSE metrics.
"""
import sys
from pathlib import Path

import fire
import numpy as np
import pandas as pd

# Add parent directory to path to import data module
sys.path.insert(0, str(Path(__file__).parent.parent))
from src.data import get_data


def mase(y_true, y_pred, y_train, seasonality):
    """
    Calculate Mean Absolute Scaled Error (MASE).

    MASE = MAE / naive_forecast_mae
    where naive_forecast uses the last observation or seasonal observation.
    """
    mae = np.mean(np.abs(y_true - y_pred))

    # Use seasonal naive as scaling factor if we have enough data
    if seasonality > 1 and len(y_train) > seasonality:
        # Seasonal naive error: compare each value with the value from the previous season
        naive_error = np.mean(np.abs(y_train[seasonality:] - y_train[:-seasonality]))
    else:
        # Regular naive error: compare each value with the previous value
        naive_error = np.mean(np.abs(y_train[1:] - y_train[:-1]))

    # Handle edge case where naive error is zero
    if naive_error == 0:
        return np.inf if mae > 0 else 0

    return mae / naive_error


def evaluate_model(fcst_df, test_df, train_df, model_name, seasonality):
    """Evaluate a single model's forecasts."""
    # Merge forecasts with test data
    merged = fcst_df.merge(test_df, on=['unique_id', 'ds'], suffixes=('_pred', '_true'))

    # Calculate metrics for each series
    metrics_list = []

    for uid in merged['unique_id'].unique():
        series_data = merged[merged['unique_id'] == uid].sort_values('ds')
        y_true = series_data['y'].values
        y_pred = series_data[model_name].values

        # Get training data for this series
        train_series = train_df[train_df['unique_id'] == uid].sort_values('ds')
        y_train = train_series['y'].values

        # Calculate metrics
        series_mase = mase(y_true, y_pred, y_train, seasonality)
        series_mae = np.mean(np.abs(y_true - y_pred))
        series_rmse = np.sqrt(np.mean((y_true - y_pred) ** 2))

        metrics_list.append({
            'unique_id': uid,
            'model': model_name,
            'mase': series_mase,
            'mae': series_mae,
            'rmse': series_rmse
        })

    metrics_df = pd.DataFrame(metrics_list)

    # Calculate aggregate metrics
    avg_mase = metrics_df['mase'].mean()
    avg_mae = metrics_df['mae'].mean()
    avg_rmse = metrics_df['rmse'].mean()

    return {
        'model': model_name,
        'mase': avg_mase,
        'mae': avg_mae,
        'rmse': avg_rmse,
        'series_count': len(metrics_df)
    }


def evaluate(group: str = 'Daily'):
    """
    Evaluate baseline model forecasts on M4 test data.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"Evaluating baseline forecasts for M4 {group} dataset...")

    # Load test data
    test_df, horizon, freq, seasonality = get_data('data', group, train=False)
    train_df, _, _, _ = get_data('data', group, train=True)

    print(f"Loaded {len(test_df)} test rows from {test_df['unique_id'].nunique()} series")
    print(f"Seasonality: {seasonality}")

    # Convert ds to datetime for test data
    test_df['ds'] = pd.to_datetime('2020-01-01') + pd.to_timedelta(test_df['ds'].astype(int) - 1, unit='D')
    test_df['ds'] = test_df['ds'].dt.date

    # Also convert train data
    train_df['ds'] = pd.to_datetime('2020-01-01') + pd.to_timedelta(train_df['ds'].astype(int) - 1, unit='D')
    train_df['ds'] = train_df['ds'].dt.date

    results_dir = Path('baseline_benchmark/results')

    # Find all forecast files
    anofox_files = list(results_dir.glob(f'anofox-*-{group}.parquet'))
    statsforecast_files = list(results_dir.glob(f'statsforecast-Baseline-{group}.parquet'))

    all_results = []

    # Evaluate Anofox models
    for file in anofox_files:
        model_name = file.stem.replace(f'anofox-', '').replace(f'-{group}', '')
        print(f"\nEvaluating anofox-{model_name}...")

        fcst_df = pd.read_parquet(file)

        # Rename columns to match expected format
        fcst_df = fcst_df.rename(columns={
            'id_cols': 'unique_id',
            'time_col': 'ds',
            'forecast_col': model_name
        })

        result = evaluate_model(fcst_df, test_df, train_df, model_name, seasonality)
        result['model'] = f'anofox-{model_name}'
        all_results.append(result)

        print(f"  MASE: {result['mase']:.3f}, MAE: {result['mae']:.2f}, RMSE: {result['rmse']:.2f}")

    # Evaluate Statsforecast models
    if statsforecast_files:
        print(f"\nEvaluating statsforecast baseline models...")
        fcst_df = pd.read_parquet(statsforecast_files[0])

        # Statsforecast returns all models in one file
        # The columns are: unique_id, ds, Naive, SeasonalNaive, RandomWalkWithDrift, WindowAverage, SeasonalWindowAverage
        for col in fcst_df.columns:
            if col not in ['unique_id', 'ds']:
                model_name = col
                print(f"\n  Evaluating statsforecast-{model_name}...")

                result = evaluate_model(fcst_df, test_df, train_df, model_name, seasonality)
                result['model'] = f'statsforecast-{model_name}'
                all_results.append(result)

                print(f"    MASE: {result['mase']:.3f}, MAE: {result['mae']:.2f}, RMSE: {result['rmse']:.2f}")

    # Create results DataFrame
    results_df = pd.DataFrame(all_results)

    # Save results
    output_file = results_dir / f'baseline-evaluation-{group}.parquet'
    results_df.to_parquet(output_file, index=False)
    print(f"\nSaved evaluation results to {output_file}")

    # Print summary table
    print(f"\n{'='*80}")
    print(f"Baseline Models Evaluation Summary - M4 {group}")
    print(f"{'='*80}")
    print(f"{'Model':<40} {'MASE':>8} {'MAE':>10} {'RMSE':>10}")
    print(f"{'-'*80}")

    for _, row in results_df.iterrows():
        print(f"{row['model']:<40} {row['mase']:>8.3f} {row['mae']:>10.2f} {row['rmse']:>10.2f}")

    print(f"{'-'*80}")

    # Print comparison
    anofox_results = results_df[results_df['model'].str.startswith('anofox-')]
    statsforecast_results = results_df[results_df['model'].str.startswith('statsforecast-')]

    if len(anofox_results) > 0 and len(statsforecast_results) > 0:
        print(f"\nAverage Performance:")
        print(f"  Anofox:        MASE={anofox_results['mase'].mean():.3f}, MAE={anofox_results['mae'].mean():.2f}, RMSE={anofox_results['rmse'].mean():.2f}")
        print(f"  Statsforecast: MASE={statsforecast_results['mase'].mean():.3f}, MAE={statsforecast_results['mae'].mean():.2f}, RMSE={statsforecast_results['rmse'].mean():.2f}")


if __name__ == '__main__':
    fire.Fire(evaluate)
