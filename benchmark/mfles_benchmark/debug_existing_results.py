"""
Phase 1: Analyze existing MFLES forecast results.

Compare Anofox vs statsforecast MFLES forecasts using existing benchmark results.
"""

import pandas as pd
import numpy as np

# Load existing forecast results
def load_forecasts():
    """Load forecast results from benchmark."""
    anofox_mfles = pd.read_parquet('results/anofox-MFLES-Daily.parquet')
    statsforecast_mfles = pd.read_parquet('results/statsforecast-MFLES-Daily.parquet')

    return anofox_mfles, statsforecast_mfles

def analyze_single_series(anofox_df, statsforecast_df, series_id):
    """Analyze forecasts for a single series."""
    anofox_series = anofox_df[anofox_df['unique_id'] == series_id].sort_values('ds')
    stats_series = statsforecast_df[statsforecast_df['unique_id'] == series_id].sort_values('ds')

    if len(anofox_series) == 0 or len(stats_series) == 0:
        print(f"Series {series_id} not found in one or both datasets")
        return None

    anofox_vals = anofox_series['MFLES'].values
    stats_vals = stats_series['MFLES'].values

    print(f"\n{'='*80}")
    print(f"PHASE 1.1: Single Series Comparison - {series_id}")
    print(f"{'='*80}\n")

    print(f"Anofox MFLES Forecasts:")
    print(f"  Mean:  {anofox_vals.mean():.2f}")
    print(f"  Std:   {anofox_vals.std():.2f}")
    print(f"  Min:   {anofox_vals.min():.2f}")
    print(f"  Max:   {anofox_vals.max():.2f}")
    print(f"  First 5: {anofox_vals[:5]}")
    print(f"  Last 5:  {anofox_vals[-5:]}")

    print(f"\nStatsforecast MFLES Forecasts:")
    print(f"  Mean:  {stats_vals.mean():.2f}")
    print(f"  Std:   {stats_vals.std():.2f}")
    print(f"  Min:   {stats_vals.min():.2f}")
    print(f"  Max:   {stats_vals.max():.2f}")
    print(f"  First 5: {stats_vals[:5]}")
    print(f"  Last 5:  {stats_vals[-5:]}")

    print(f"\nDifference (Anofox - Statsforecast):")
    diff = anofox_vals - stats_vals
    print(f"  Mean Diff:  {diff.mean():.2f}")
    print(f"  Mean Abs Diff: {np.abs(diff).mean():.2f}")
    print(f"  Max Abs Diff:  {np.abs(diff).max():.2f}")

    # Percentage difference
    # Avoid division by zero
    pct_diff = np.where(stats_vals != 0, (anofox_vals - stats_vals) / np.abs(stats_vals) * 100, 0)
    print(f"  Mean % Diff: {pct_diff.mean():.2f}%")
    print(f"  Median % Diff: {np.median(pct_diff):.2f}%")

    print(f"\nPoint-by-point comparison:")
    comparison_df = pd.DataFrame({
        'Horizon': range(1, len(anofox_vals)+1),
        'Anofox': anofox_vals,
        'Statsforecast': stats_vals,
        'Diff': diff,
        'Pct_Diff': pct_diff
    })
    print(comparison_df.to_string(index=False))

    return comparison_df

def analyze_overall_statistics(anofox_df, statsforecast_df):
    """Analyze overall forecast statistics."""
    print(f"\n{'='*80}")
    print("PHASE 1.2: Overall Forecast Statistics")
    print(f"{'='*80}\n")

    anofox_vals = anofox_df['MFLES'].values
    stats_vals = statsforecast_df['MFLES'].values

    print(f"Anofox MFLES (all series):")
    print(f"  Count:  {len(anofox_vals)}")
    print(f"  Mean:   {anofox_vals.mean():.2f}")
    print(f"  Median: {np.median(anofox_vals):.2f}")
    print(f"  Std:    {anofox_vals.std():.2f}")
    print(f"  Min:    {anofox_vals.min():.2f}")
    print(f"  Max:    {anofox_vals.max():.2f}")
    print(f"  Extreme values (>100k): {(anofox_vals > 100000).sum()}")
    print(f"  Negative values (<0): {(anofox_vals < 0).sum()}")

    print(f"\nStatsforecast MFLES (all series):")
    print(f"  Count:  {len(stats_vals)}")
    print(f"  Mean:   {stats_vals.mean():.2f}")
    print(f"  Median: {np.median(stats_vals):.2f}")
    print(f"  Std:    {stats_vals.std():.2f}")
    print(f"  Min:    {stats_vals.min():.2f}")
    print(f"  Max:    {stats_vals.max():.2f}")
    print(f"  Extreme values (>100k): {(stats_vals > 100000).sum()}")
    print(f"  Negative values (<0): {(stats_vals < 0).sum()}")

    # Find series with extreme values
    print(f"\nSeries with extreme Anofox forecasts (>100k or <0):")
    extreme_series = anofox_df[(anofox_df['MFLES'] > 100000) | (anofox_df['MFLES'] < 0)]['unique_id'].unique()
    print(f"  Count: {len(extreme_series)}")
    if len(extreme_series) > 0:
        print(f"  Examples: {list(extreme_series[:10])}")

def find_worst_series(anofox_df, statsforecast_df):
    """Find series with the worst forecast differences."""
    print(f"\n{'='*80}")
    print("Finding series with largest forecast differences...")
    print(f"{'='*80}\n")

    # Merge on unique_id and ds
    merged = anofox_df.merge(
        statsforecast_df,
        on=['unique_id', 'ds'],
        suffixes=('_anofox', '_statsforecast')
    )

    # Calculate absolute difference per series
    merged['abs_diff'] = np.abs(merged['MFLES_anofox'] - merged['MFLES_statsforecast'])
    series_diff = merged.groupby('unique_id')['abs_diff'].mean().sort_values(ascending=False)

    print("Top 10 series with largest mean absolute difference:")
    print(series_diff.head(10))

    return series_diff.head(10).index.tolist()

if __name__ == "__main__":
    print("Loading MFLES forecast results...")
    anofox_df, statsforecast_df = load_forecasts()
    print(f"Loaded Anofox: {len(anofox_df)} forecasts")
    print(f"Loaded Statsforecast: {len(statsforecast_df)} forecasts")

    # Overall statistics
    analyze_overall_statistics(anofox_df, statsforecast_df)

    # Find worst series
    worst_series = find_worst_series(anofox_df, statsforecast_df)

    # Analyze worst series in detail
    if len(worst_series) > 0:
        print(f"\n{'='*80}")
        print(f"Analyzing worst series: {worst_series[0]}")
        print(f"{'='*80}")
        analyze_single_series(anofox_df, statsforecast_df, worst_series[0])

    # Also analyze first series (D1) for comparison
    print(f"\n{'='*80}")
    print("Analyzing first series: D1")
    print(f"{'='*80}")
    result = analyze_single_series(anofox_df, statsforecast_df, 'D1')

    if result is not None:
        # Save for unit testing
        result.to_parquet('debug_results/phase1_single_series_comparison.parquet')
        print(f"\nResults saved to debug_results/phase1_single_series_comparison.parquet")
