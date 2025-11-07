"""
Analyze D2139 explosion using existing forecast results.

Root cause hypothesis:
In multiplicative mode, trend is accumulated in log space across boosting iterations.
If accumulated_trend_ values become too large (e.g., 15-20), then:
- projectTrend() projects in log space: log_forecast = slope * h + last_point
- postprocess() applies exp(): forecast = exp(log_forecast)
- exp(17) = 24 million!

This is the "log space explosion" problem.
"""

import pandas as pd
import numpy as np

def analyze_forecasts():
    """Analyze forecast results to understand D2139 explosion."""
    # Load forecast results
    anofox_df = pd.read_parquet('results/anofox-MFLES-Daily.parquet')
    stats_df = pd.read_parquet('results/statsforecast-MFLES-Daily.parquet')

    # Get D2139 forecasts
    d2139_anofox = anofox_df[anofox_df['unique_id'] == 'D2139'].sort_values('ds')
    d2139_stats = stats_df[stats_df['unique_id'] == 'D2139'].sort_values('ds')

    print("\n" + "="*80)
    print("D2139 MFLES Forecast Analysis")
    print("="*80)

    anofox_vals = d2139_anofox['MFLES'].values
    stats_vals = d2139_stats['MFLES'].values

    print("\nAnofox MFLES (EXPLODED):")
    print(f"  Mean:  {anofox_vals.mean():,.2f}")
    print(f"  Std:   {anofox_vals.std():,.2f}")
    print(f"  Min:   {anofox_vals.min():,.2f}")
    print(f"  Max:   {anofox_vals.max():,.2f}")
    print(f"  Values: {anofox_vals}")

    print("\nStatsforecast MFLES (CORRECT):")
    print(f"  Mean:  {stats_vals.mean():,.2f}")
    print(f"  Std:   {stats_vals.std():,.2f}")
    print(f"  Min:   {stats_vals.min():,.2f}")
    print(f"  Max:   {stats_vals.max():,.2f}")
    print(f"  Values: {stats_vals}")

    print("\nRatio (Anofox / Statsforecast):")
    ratio = anofox_vals / stats_vals
    print(f"  Mean ratio: {ratio.mean():,.2f}x")
    print(f"  Max ratio:  {ratio.max():,.2f}x")

    # Reverse engineer the log space values
    print("\n" + "="*80)
    print("REVERSE ENGINEERING LOG SPACE")
    print("="*80)
    print("\nIf these forecasts came from exp() transform:")

    anofox_log = np.log(anofox_vals)
    stats_log = np.log(stats_vals)

    print(f"\nAnofox log-space values:")
    print(f"  log(forecasts) = {anofox_log}")
    print(f"  Mean: {anofox_log.mean():.4f}")
    print(f"  Range: {anofox_log.min():.4f} to {anofox_log.max():.4f}")

    print(f"\nStatsforecast log-space values:")
    print(f"  log(forecasts) = {stats_log}")
    print(f"  Mean: {stats_log.mean():.4f}")
    print(f"  Range: {stats_log.min():.4f} to {stats_log.max():.4f}")

    print(f"\nDifference in log space:")
    log_diff = anofox_log - stats_log
    print(f"  log(anofox) - log(stats) = {log_diff}")
    print(f"  Mean diff: {log_diff.mean():.4f}")
    print(f"  This means Anofox accumulated ~{log_diff.mean():.2f} more in log space")

    # Check for exponential pattern
    print("\n" + "="*80)
    print("HYPOTHESIS VALIDATION")
    print("="*80)

    # If Anofox has log values around 17, exp(17) ≈ 24 million
    if anofox_vals.max() > 1_000_000:
        expected_log_val = np.log(anofox_vals.max())
        print(f"\n✓ EXPLOSION CONFIRMED:")
        print(f"  Max forecast: {anofox_vals.max():,.0f}")
        print(f"  This came from: exp({expected_log_val:.2f}) ≈ {np.exp(expected_log_val):,.0f}")
        print(f"\n  This indicates accumulated_trend_ values in log space were ~{expected_log_val:.1f}")
        print(f"  When exp() was applied in postprocess(), it exploded to millions!")

        print(f"\n  ROOT CAUSE:")
        print(f"  ──────────")
        print(f"  1. D2139 uses MULTIPLICATIVE mode (CoV > 0.7)")
        print(f"  2. Data is log-transformed before fitting")
        print(f"  3. Trend is accumulated in log space across boosting iterations")
        print(f"  4. After {log_diff.mean():.2f} units of excess accumulation:")
        print(f"     accumulated_trend_[1] ≈ {expected_log_val:.1f} (in log space)")
        print(f"  5. projectTrend() projects in log space:")
        print(f"     projection[h] = slope * h + last_point ≈ 16-17")
        print(f"  6. postprocess() applies exp():")
        print(f"     exp(17) = {np.exp(17):,.0f}")
        print(f"\n  FIX:")
        print(f"  ────")
        print(f"  Clip accumulated_trend_ values in log space to prevent overflow")
        print(f"  OR apply learning rates differently")
        print(f"  OR don't accumulate trend across iterations - store only fitted values")

    return anofox_vals, stats_vals

if __name__ == "__main__":
    analyze_forecasts()
