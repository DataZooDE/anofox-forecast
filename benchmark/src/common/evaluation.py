"""
Shared evaluation module for forecast benchmarks.

Evaluates forecast results using MASE, MAE, and RMSE metrics with Polars expressions.
"""
from pathlib import Path
from typing import List, Dict

import polars as pl
import pandas as pd


def evaluate_model(fcst_df, test_df, train_df, model_name, seasonality):
    """Evaluate a single model's forecasts using Polars expressions."""
    # Join forecasts with test data
    merged = fcst_df.join(
        test_df,
        on=['unique_id', 'ds'],
        how='inner'
    )

    # Calculate errors using expressions
    merged = merged.with_columns([
        (pl.col('y') - pl.col(model_name)).abs().alias('error'),
        ((pl.col('y') - pl.col(model_name)) ** 2).alias('squared_error')
    ])

    # Calculate MAE and RMSE per series using group_by with expressions
    metrics_df = merged.group_by('unique_id').agg([
        pl.col('error').mean().alias('mae'),
        pl.col('squared_error').mean().sqrt().alias('rmse')
    ])

    # Calculate scaling factors for MASE using Polars expressions
    # For each series, calculate naive error based on seasonality
    if seasonality > 1:
        # Seasonal naive: compare values with previous season
        train_scales = train_df.sort(['unique_id', 'ds']).group_by('unique_id').agg([
            (pl.col('y').diff(seasonality).abs().mean()).alias('scale')
        ])
    else:
        # Regular naive: compare consecutive values
        train_scales = train_df.sort(['unique_id', 'ds']).group_by('unique_id').agg([
            (pl.col('y').diff().abs().mean()).alias('scale')
        ])

    # Replace zero scales with infinity to avoid division by zero
    train_scales = train_scales.with_columns([
        pl.when(pl.col('scale') == 0).then(pl.lit(float('inf'))).otherwise(pl.col('scale')).alias('scale')
    ])

    # Join metrics with scales and calculate MASE
    metrics_df = metrics_df.join(train_scales, on='unique_id', how='left')
    metrics_df = metrics_df.with_columns([
        (pl.col('mae') / pl.col('scale')).alias('mase')
    ])

    # Calculate aggregate metrics
    summary = metrics_df.select([
        pl.col('mase').mean().alias('avg_mase'),
        pl.col('mae').mean().alias('avg_mae'),
        pl.col('rmse').mean().alias('avg_rmse'),
        pl.len().alias('series_count')
    ]).row(0)

    return {
        'model': model_name,
        'mase': summary[0],
        'mae': summary[1],
        'rmse': summary[2],
        'series_count': summary[3]
    }


def evaluate_forecasts(
    benchmark_name: str,
    test_df_pd: pd.DataFrame,
    train_df_pd: pd.DataFrame,
    seasonality: int,
    results_dir: Path,
    group: str = 'Daily'
):
    """
    Evaluate forecast results for a benchmark.

    Parameters
    ----------
    benchmark_name : str
        Name of the benchmark (e.g., 'baseline', 'ets', 'theta')
    test_df_pd : pd.DataFrame
        Test data with columns [unique_id, ds, y]
    train_df_pd : pd.DataFrame
        Training data with columns [unique_id, ds, y]
    seasonality : int
        Seasonal period
    results_dir : Path
        Directory containing forecast results
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'

    Returns
    -------
    pl.DataFrame
        Evaluation results containing MASE, MAE, RMSE for each model
    """
    print(f"Evaluating {benchmark_name} forecasts for M4 {group} dataset...")

    # Convert pandas to polars
    test_df = pl.from_pandas(test_df_pd)
    train_df = pl.from_pandas(train_df_pd)

    print(f"Loaded {len(test_df)} test rows from {test_df['unique_id'].n_unique()} series")
    print(f"Seasonality: {seasonality}")

    # Convert ds to date using Polars expressions
    test_df = test_df.with_columns([
        (pl.datetime(2020, 1, 1) + pl.duration(days=pl.col('ds').cast(pl.Int64) - 1)).dt.date().alias('ds')
    ])

    train_df = train_df.with_columns([
        (pl.datetime(2020, 1, 1) + pl.duration(days=pl.col('ds').cast(pl.Int64) - 1)).dt.date().alias('ds')
    ])

    # Find all forecast files
    anofox_files = list(results_dir.glob(f'anofox-*-{group}.parquet'))
    statsforecast_files = list(results_dir.glob(f'statsforecast-*-{group}.parquet'))

    all_results = []

    # Evaluate Anofox models
    for file in anofox_files:
        model_name = file.stem.replace(f'anofox-', '').replace(f'-{group}', '')
        print(f"\nEvaluating anofox-{model_name}...")

        fcst_df = pl.read_parquet(file)

        # Rename columns to match expected format
        fcst_df = fcst_df.rename({
            'id_cols': 'unique_id',
            'time_col': 'ds',
            'forecast_col': model_name
        })

        # Convert ds to date to match test_df
        if fcst_df['ds'].dtype in [pl.Datetime, pl.Datetime('ms'), pl.Datetime('us'), pl.Datetime('ns')]:
            fcst_df = fcst_df.with_columns([pl.col('ds').dt.date()])
        elif fcst_df['ds'].dtype == pl.Utf8:
            fcst_df = fcst_df.with_columns([pl.col('ds').str.to_date().cast(pl.Date)])

        result = evaluate_model(fcst_df, test_df, train_df, model_name, seasonality)
        result['model'] = f'anofox-{model_name}'
        all_results.append(result)

        print(f"  MASE: {result['mase']:.3f}, MAE: {result['mae']:.2f}, RMSE: {result['rmse']:.2f}")

    # Evaluate Statsforecast models
    if statsforecast_files:
        print(f"\nEvaluating statsforecast {benchmark_name} models...")
        fcst_df = pl.read_parquet(statsforecast_files[0])

        # Convert ds to date to match test_df
        if fcst_df['ds'].dtype in [pl.Datetime, pl.Datetime('ms'), pl.Datetime('us'), pl.Datetime('ns')]:
            fcst_df = fcst_df.with_columns([pl.col('ds').dt.date()])
        elif fcst_df['ds'].dtype == pl.Utf8:
            fcst_df = fcst_df.with_columns([pl.col('ds').str.to_date().cast(pl.Date)])

        # Statsforecast returns all models in one file
        # Filter out non-model columns (unique_id, ds, index, etc.)
        model_columns = [col for col in fcst_df.columns if col not in ['unique_id', 'ds', 'index']]

        for model_name in model_columns:
            print(f"\n  Evaluating statsforecast-{model_name}...")

            result = evaluate_model(fcst_df, test_df, train_df, model_name, seasonality)
            result['model'] = f'statsforecast-{model_name}'
            all_results.append(result)

            print(f"    MASE: {result['mase']:.3f}, MAE: {result['mae']:.2f}, RMSE: {result['rmse']:.2f}")

    # Create results DataFrame
    results_df = pl.DataFrame(all_results)

    # Save results
    output_file = results_dir / f'{benchmark_name}-evaluation-{group}.parquet'
    results_df.write_parquet(output_file)
    print(f"\nSaved evaluation results to {output_file}")

    # Print summary table
    print(f"\n{'='*80}")
    print(f"{benchmark_name.capitalize()} Models Evaluation Summary - M4 {group}")
    print(f"{'='*80}")
    print(f"{'Model':<40} {'MASE':>8} {'MAE':>10} {'RMSE':>10}")
    print(f"{'-'*80}")

    for row in results_df.iter_rows(named=True):
        print(f"{row['model']:<40} {row['mase']:>8.3f} {row['mae']:>10.2f} {row['rmse']:>10.2f}")

    print(f"{'-'*80}")

    # Print comparison
    anofox_results = results_df.filter(pl.col('model').str.starts_with('anofox-'))
    statsforecast_results = results_df.filter(pl.col('model').str.starts_with('statsforecast-'))

    if len(anofox_results) > 0 and len(statsforecast_results) > 0:
        print(f"\nAverage Performance:")
        anofox_avg = anofox_results.select([
            pl.col('mase').mean(),
            pl.col('mae').mean(),
            pl.col('rmse').mean()
        ]).row(0)
        stats_avg = statsforecast_results.select([
            pl.col('mase').mean(),
            pl.col('mae').mean(),
            pl.col('rmse').mean()
        ]).row(0)
        print(f"  Anofox:        MASE={anofox_avg[0]:.3f}, MAE={anofox_avg[1]:.2f}, RMSE={anofox_avg[2]:.2f}")
        print(f"  Statsforecast: MASE={stats_avg[0]:.3f}, MAE={stats_avg[1]:.2f}, RMSE={stats_avg[2]:.2f}")

    return results_df
