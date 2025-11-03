"""
Evaluation script for Theta benchmark results.

Computes MASE, MAE, and RMSE for all Theta variants.
"""
import sys
from pathlib import Path

import fire
import pandas as pd
import numpy as np

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))
from src.data import get_data


def mase(y_true, y_pred, y_train, seasonality):
    """
    Calculate Mean Absolute Scaled Error (MASE).

    Parameters
    ----------
    y_true : array-like
        Actual values
    y_pred : array-like
        Predicted values
    y_train : array-like
        Training data for naive seasonal forecast
    seasonality : int
        Seasonal period

    Returns
    -------
    float
        MASE value
    """
    mae = np.mean(np.abs(y_true - y_pred))

    # Naive seasonal forecast error
    if seasonality > 1 and len(y_train) > seasonality:
        naive_error = np.mean(np.abs(y_train[seasonality:] - y_train[:-seasonality]))
    else:
        # Fallback to simple naive forecast
        naive_error = np.mean(np.abs(y_train[1:] - y_train[:-1]))

    if naive_error == 0:
        return np.inf if mae > 0 else 0

    return mae / naive_error


def evaluate(group: str = 'Daily'):
    """
    Evaluate Theta forecast results.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"Evaluating Theta forecasts for {group}...")

    # Load test data
    train_df, horizon, freq, seasonality = get_data('data', group, train=True)
    test_df, _, _, _ = get_data('data', group, train=False)

    # Convert test data ds (integer indices) to actual dates to match forecasts
    # Use vectorized operation for speed
    base_date = pd.Timestamp('2020-01-01')
    test_df['ds'] = base_date + pd.to_timedelta(test_df['ds'].astype(int) - 1, unit='D')

    print(f"Test set: {len(test_df)} rows from {test_df['unique_id'].nunique()} series")
    print(f"Test data date range: {test_df['ds'].min()} to {test_df['ds'].max()}")

    results_dir = Path('theta_benchmark/results')

    # Find all forecast files
    anofox_files = list(results_dir.glob(f'anofox-*-{group}.parquet'))
    statsforecast_file = results_dir / f'statsforecast-Theta-{group}.parquet'

    all_results = []

    # Evaluate Anofox models
    for forecast_file in anofox_files:
        model_name = forecast_file.stem.replace(f'-{group}', '')
        print(f"\nEvaluating {model_name}...")

        try:
            fcst_df = pd.read_parquet(forecast_file)

            # Match column names
            fcst_df = fcst_df.rename(columns={
                'id_cols': 'unique_id',
                'time_col': 'ds',
                'forecast_col': 'forecast'
            })

            # Convert ds to datetime to match test_df
            fcst_df['ds'] = pd.to_datetime(fcst_df['ds'])

            # Merge with test data
            merged = test_df.merge(fcst_df[['unique_id', 'ds', 'forecast']],
                                   on=['unique_id', 'ds'],
                                   how='inner')

            if len(merged) == 0:
                print(f"WARNING: No matching forecasts found for {model_name}")
                continue

            # Calculate metrics per series
            series_metrics = []
            for uid in merged['unique_id'].unique():
                series_data = merged[merged['unique_id'] == uid].sort_values('ds')
                train_data = train_df[train_df['unique_id'] == uid]['y'].values

                y_true = series_data['y'].values
                y_pred = series_data['forecast'].values

                if len(y_true) > 0 and len(y_pred) > 0:
                    mae_val = np.mean(np.abs(y_true - y_pred))
                    rmse_val = np.sqrt(np.mean((y_true - y_pred) ** 2))
                    mase_val = mase(y_true, y_pred, train_data, seasonality)

                    series_metrics.append({
                        'unique_id': uid,
                        'mae': mae_val,
                        'rmse': rmse_val,
                        'mase': mase_val
                    })

            # Aggregate metrics
            metrics_df = pd.DataFrame(series_metrics)
            avg_mase = metrics_df['mase'].mean()
            avg_mae = metrics_df['mae'].mean()
            avg_rmse = metrics_df['rmse'].mean()

            # Load timing metrics
            metrics_file = forecast_file.parent / f'{model_name}-{group}-metrics.parquet'
            if metrics_file.exists():
                timing_df = pd.read_parquet(metrics_file)
                time_seconds = timing_df['time_seconds'].iloc[0]
            else:
                time_seconds = None

            all_results.append({
                'model': model_name,
                'group': group,
                'MASE': avg_mase,
                'MAE': avg_mae,
                'RMSE': avg_rmse,
                'time_seconds': time_seconds,
                'series_count': len(series_metrics)
            })

            print(f"  MASE: {avg_mase:.3f}")
            print(f"  MAE: {avg_mae:.2f}")
            print(f"  RMSE: {avg_rmse:.2f}")
            if time_seconds:
                print(f"  Time: {time_seconds:.2f}s")

        except Exception as e:
            print(f"ERROR evaluating {model_name}: {e}")
            import traceback
            traceback.print_exc()
            continue

    # Evaluate Statsforecast models
    if statsforecast_file.exists():
        print(f"\nEvaluating statsforecast models...")

        try:
            fcst_df = pd.read_parquet(statsforecast_file)

            # Convert ds to datetime to match test_df
            fcst_df['ds'] = pd.to_datetime(fcst_df['ds'])

            # Get model columns (exclude unique_id, ds, and index)
            model_columns = [col for col in fcst_df.columns
                           if col not in ['unique_id', 'ds', 'index'] and not col.endswith('-lo-95') and not col.endswith('-hi-95')]

            for model_col in model_columns:
                print(f"\nEvaluating statsforecast-{model_col}...")

                # Merge with test data
                merged = test_df.merge(fcst_df[['unique_id', 'ds', model_col]],
                                       on=['unique_id', 'ds'],
                                       how='inner')

                if len(merged) == 0:
                    print(f"WARNING: No matching forecasts found")
                    continue

                # Calculate metrics per series
                series_metrics = []
                for uid in merged['unique_id'].unique():
                    series_data = merged[merged['unique_id'] == uid].sort_values('ds')
                    train_data = train_df[train_df['unique_id'] == uid]['y'].values

                    y_true = series_data['y'].values
                    y_pred = series_data[model_col].values

                    if len(y_true) > 0 and len(y_pred) > 0:
                        mae_val = np.mean(np.abs(y_true - y_pred))
                        rmse_val = np.sqrt(np.mean((y_true - y_pred) ** 2))
                        mase_val = mase(y_true, y_pred, train_data, seasonality)

                        series_metrics.append({
                            'unique_id': uid,
                            'mae': mae_val,
                            'rmse': rmse_val,
                            'mase': mase_val
                        })

                # Aggregate metrics
                metrics_df = pd.DataFrame(series_metrics)
                avg_mase = metrics_df['mase'].mean()
                avg_mae = metrics_df['mae'].mean()
                avg_rmse = metrics_df['rmse'].mean()

                # Load timing metrics
                timing_file = results_dir / f'statsforecast-Theta-{group}-metrics.parquet'
                if timing_file.exists():
                    timing_df = pd.read_parquet(timing_file)
                    model_timing = timing_df[timing_df['model'] == f'statsforecast-{model_col}']
                    time_seconds = model_timing['time_seconds'].iloc[0] if len(model_timing) > 0 else None
                else:
                    time_seconds = None

                all_results.append({
                    'model': f'statsforecast-{model_col}',
                    'group': group,
                    'MASE': avg_mase,
                    'MAE': avg_mae,
                    'RMSE': avg_rmse,
                    'time_seconds': time_seconds,
                    'series_count': len(series_metrics)
                })

                print(f"  MASE: {avg_mase:.3f}")
                print(f"  MAE: {avg_mae:.2f}")
                print(f"  RMSE: {avg_rmse:.2f}")
                if time_seconds:
                    print(f"  Time: {time_seconds:.2f}s")

        except Exception as e:
            print(f"ERROR evaluating statsforecast: {e}")
            import traceback
            traceback.print_exc()

    # Save results
    if all_results:
        results_df = pd.DataFrame(all_results)
        output_file = results_dir / f'evaluation-Theta-{group}.parquet'
        results_df.to_parquet(output_file, index=False)
        print(f"\n✅ Evaluation results saved to {output_file}")

        # Print summary table
        print(f"\n{'='*80}")
        print("THETA BENCHMARK RESULTS")
        print(f"{'='*80}")
        print(results_df.to_string(index=False))
    else:
        print("\n❌ No results to save")


if __name__ == '__main__':
    fire.Fire(evaluate)
