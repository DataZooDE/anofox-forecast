"""
Direct forecast comparison between anofox-MFLES and statsforecast-MFLES
Uses the existing benchmark parquet files
"""

import duckdb
import pandas as pd
import numpy as np
from pathlib import Path

def load_forecasts():
    """Load forecast parquet files using DuckDB."""
    results_dir = Path(__file__).parent / "results"

    anofox_file = results_dir / "anofox-MFLES-Daily.parquet"
    stats_file = results_dir / "statsforecast-MFLES-Daily.parquet"

    con = duckdb.connect()

    # Load anofox forecasts
    anofox = con.execute(f"""
        SELECT *
        FROM '{anofox_file}'
        ORDER BY id_cols, time_col
    """).df()

    # Load statsforecast forecasts
    stats = con.execute(f"""
        SELECT *
        FROM '{stats_file}'
        ORDER BY unique_id, ds
    """).df()

    con.close()

    return anofox, stats

def align_forecasts(anofox, stats):
    """Align the two forecast dataframes for comparison."""

    # Rename columns to match
    anofox_aligned = anofox.rename(columns={
        'id_cols': 'unique_id',
        'time_col': 'ds',
        'forecast_col': 'anofox_forecast'
    })[['unique_id', 'ds', 'anofox_forecast']]

    stats_aligned = stats.rename(columns={
        'MFLES': 'stats_forecast'
    })[['unique_id', 'ds', 'stats_forecast']]

    # Merge on series ID and timestamp
    merged = anofox_aligned.merge(stats_aligned, on=['unique_id', 'ds'], how='inner')

    return merged

def analyze_series(df, series_id, n_series=5):
    """Analyze forecast differences for specific series."""

    series_data = df[df['unique_id'] == series_id].copy()

    if len(series_data) == 0:
        print(f"Series {series_id} not found")
        return

    series_data['diff'] = series_data['anofox_forecast'] - series_data['stats_forecast']
    series_data['diff_pct'] = (series_data['diff'] / series_data['stats_forecast'].abs()) * 100
    series_data['abs_diff'] = series_data['diff'].abs()

    print(f"\n{'='*80}")
    print(f"Series: {series_id}")
    print(f"{'='*80}")

    print(f"\n{'Step':<6} {'Anofox':<12} {'Stats':<12} {'Diff':<12} {'Diff %':<10}")
    print("-"*60)

    for idx, row in series_data.iterrows():
        print(f"{row['ds']:<6} {row['anofox_forecast']:<12.4f} {row['stats_forecast']:<12.4f} "
              f"{row['diff']:<12.4f} {row['diff_pct']:<10.2f}")

    print(f"\n{'Summary':<20} {'Value':<12}")
    print("-"*35)
    print(f"{'Mean Abs Diff':<20} {series_data['abs_diff'].mean():<12.4f}")
    print(f"{'Max Abs Diff':<20} {series_data['abs_diff'].max():<12.4f}")
    print(f"{'RMSE':<20} {np.sqrt((series_data['diff']**2).mean()):<12.4f}")
    print(f"{'Mean Diff %':<20} {series_data['diff_pct'].abs().mean():<12.2f}")

def find_worst_series(df, n=10):
    """Find series with largest forecast differences."""

    series_errors = df.groupby('unique_id').apply(
        lambda x: np.sqrt(((x['anofox_forecast'] - x['stats_forecast'])**2).mean())
    ).sort_values(ascending=False)

    print(f"\n{'='*80}")
    print(f"Top {n} Series with Largest RMSE Difference")
    print(f"{'='*80}\n")

    print(f"{'Rank':<6} {'Series ID':<12} {'RMSE':<12}")
    print("-"*35)

    for i, (series_id, rmse) in enumerate(series_errors.head(n).items(), 1):
        print(f"{i:<6} {series_id:<12} {rmse:<12.4f}")

    return list(series_errors.head(n).index)

def overall_comparison(df):
    """Overall statistics comparing the two implementations."""

    df['diff'] = df['anofox_forecast'] - df['stats_forecast']
    df['abs_diff'] = df['diff'].abs()
    df['sq_diff'] = df['diff'] ** 2

    print(f"\n{'='*80}")
    print("Overall Forecast Comparison")
    print(f"{'='*80}\n")

    print(f"{'Metric':<30} {'Value':<12}")
    print("-"*45)
    print(f"{'Total forecast points':<30} {len(df):<12}")
    print(f"{'Mean Absolute Difference':<30} {df['abs_diff'].mean():<12.4f}")
    print(f"{'Median Absolute Difference':<30} {df['abs_diff'].median():<12.4f}")
    print(f"{'Max Absolute Difference':<30} {df['abs_diff'].max():<12.4f}")
    print(f"{'RMSE':<30} {np.sqrt(df['sq_diff'].mean()):<12.4f}")
    print(f"{'Correlation':<30} {df['anofox_forecast'].corr(df['stats_forecast']):<12.6f}")

    # Distribution of differences
    print(f"\n{'Difference Distribution':<30}")
    print("-"*45)
    print(f"{'5th percentile':<30} {df['diff'].quantile(0.05):<12.4f}")
    print(f"{'25th percentile':<30} {df['diff'].quantile(0.25):<12.4f}")
    print(f"{'50th percentile (median)':<30} {df['diff'].quantile(0.50):<12.4f}")
    print(f"{'75th percentile':<30} {df['diff'].quantile(0.75):<12.4f}")
    print(f"{'95th percentile':<30} {df['diff'].quantile(0.95):<12.4f}")

def main():
    """Run forecast comparison analysis."""

    print("Loading forecast data...")
    anofox, stats = load_forecasts()

    print(f"Anofox forecasts: {len(anofox)} points")
    print(f"Statsforecast forecasts: {len(stats)} points")

    print("\nAligning forecasts...")
    df = align_forecasts(anofox, stats)

    print(f"Matched forecast points: {len(df)}")
    print(f"Unique series: {df['unique_id'].nunique()}")

    # Overall comparison
    overall_comparison(df)

    # Find worst series
    worst_series = find_worst_series(df, n=10)

    # Analyze top 3 worst series in detail
    print(f"\n{'='*80}")
    print("Detailed Analysis of Top 3 Worst Series")
    print(f"{'='*80}")

    for series_id in worst_series[:3]:
        analyze_series(df, series_id)

    # Also analyze some good series for comparison
    series_errors = df.groupby('unique_id').apply(
        lambda x: np.sqrt(((x['anofox_forecast'] - x['stats_forecast'])**2).mean())
    ).sort_values(ascending=True)

    best_series = list(series_errors.head(3).index)

    print(f"\n{'='*80}")
    print("Detailed Analysis of Top 3 Best Series (for comparison)")
    print(f"{'='*80}")

    for series_id in best_series:
        analyze_series(df, series_id)

if __name__ == "__main__":
    main()
