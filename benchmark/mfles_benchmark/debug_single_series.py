"""
Phase 1.1: Compare MFLES forecast outputs on a single series.

This test compares Anofox vs statsforecast MFLES on a single M4 Daily series
to understand the accuracy discrepancy.
"""

import duckdb
import pandas as pd
import numpy as np
from statsforecast.models import MFLES

# Load M4 Daily data
def load_single_series(series_id='D1'):
    """Load a single series from M4 Daily dataset."""
    conn = duckdb.connect(database=':memory:')
    conn.execute("INSTALL parquet")
    conn.execute("LOAD parquet")

    # Load data
    df = conn.execute("""
        SELECT * FROM read_parquet('../data/m4/m4-daily.parquet')
        WHERE unique_id = ?
        ORDER BY ds
    """, [series_id]).fetchdf()

    return df

def test_anofox_single_series(df, horizon=14, seasonality=7):
    """Test Anofox MFLES on a single series."""
    conn = duckdb.connect(database=':memory:')

    # Load extension
    conn.execute("LOAD '/home/simonm/projects/duckdb/anofox-forecast/build/release/extension/anofox/anofox.duckdb_extension'")

    # Create temp table
    conn.register('series_data', df)

    # Run MFLES forecast
    result = conn.execute(f"""
        SELECT
            unique_id,
            ds,
            y,
            yhat
        FROM TS_FORECAST_BY(
            TABLE series_data,
            unique_id,
            ds,
            y,
            'MFLES',
            '{{"horizon": {horizon}, "season_length": {seasonality}}}'
        )
        WHERE ds > (SELECT MAX(ds) FROM series_data)
        ORDER BY ds
    """).fetchdf()

    return result

def test_statsforecast_single_series(df, horizon=14, seasonality=7):
    """Test statsforecast MFLES on a single series."""
    # Prepare data
    y = df['y'].values

    # Fit model
    model = MFLES(season_length=seasonality)
    model.fit(y)

    # Forecast
    forecasts = model.predict(h=horizon)

    # Create result dataframe
    last_date = df['ds'].max()
    forecast_dates = pd.date_range(start=last_date + pd.Timedelta(days=1), periods=horizon, freq='D')

    result = pd.DataFrame({
        'unique_id': df['unique_id'].iloc[0],
        'ds': forecast_dates,
        'yhat': forecasts['mean'].values if isinstance(forecasts, pd.DataFrame) else forecasts
    })

    return result

def compare_forecasts(anofox_result, statsforecast_result):
    """Compare forecast outputs."""
    print("\n" + "="*80)
    print("PHASE 1.1: Single Series Comparison (D1)")
    print("="*80)

    anofox_vals = anofox_result['yhat'].values
    stats_vals = statsforecast_result['yhat'].values

    print(f"\nAnofox MFLES Forecasts:")
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
    pct_diff = (anofox_vals - stats_vals) / stats_vals * 100
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

    # Test result
    print(f"\n" + "-"*80)
    if np.abs(pct_diff.mean()) < 10:
        print("✅ TEST PASSED: Forecasts are similar (within 10% on average)")
        return True
    else:
        print("❌ TEST FAILED: Forecasts differ significantly")
        return False

if __name__ == "__main__":
    print("Loading M4 Daily series D1...")
    df = load_single_series('D1')
    print(f"Loaded {len(df)} observations")
    print(f"Value range: {df['y'].min():.2f} to {df['y'].max():.2f}")
    print(f"Mean: {df['y'].mean():.2f}, Std: {df['y'].std():.2f}")

    print("\n" + "="*80)
    print("Running Anofox MFLES...")
    print("="*80)
    anofox_result = test_anofox_single_series(df)
    print(f"Generated {len(anofox_result)} forecast points")

    print("\n" + "="*80)
    print("Running Statsforecast MFLES...")
    print("="*80)
    statsforecast_result = test_statsforecast_single_series(df)
    print(f"Generated {len(statsforecast_result)} forecast points")

    # Compare
    success = compare_forecasts(anofox_result, statsforecast_result)

    # Save results for unit testing
    comparison_df = pd.DataFrame({
        'horizon': range(1, 15),
        'anofox_forecast': anofox_result['yhat'].values,
        'statsforecast_forecast': statsforecast_result['yhat'].values
    })
    comparison_df.to_parquet('debug_results/phase1_1_single_series.parquet')
    print(f"\nResults saved to debug_results/phase1_1_single_series.parquet")

    exit(0 if success else 1)
