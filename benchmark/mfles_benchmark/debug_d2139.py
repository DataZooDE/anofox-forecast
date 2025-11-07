"""
Diagnostic script for D2139 explosion issue.

Investigate why D2139 produces forecasts of 27 million when statsforecast produces ~22,617.
"""

import duckdb
import pandas as pd
import numpy as np

def load_d2139_data():
    """Load D2139 series from M4 Daily dataset."""
    conn = duckdb.connect(database=':memory:')

    # Load data
    df = conn.execute("""
        SELECT * FROM read_parquet('../data/m4/m4-daily.parquet')
        WHERE unique_id = 'D2139'
        ORDER BY ds
    """).fetchdf()

    return df

def analyze_series_characteristics(df):
    """Analyze series characteristics to understand why it's problematic."""
    y = df['y'].values

    print("\n" + "="*80)
    print("D2139 Series Characteristics")
    print("="*80)
    print(f"Length: {len(y)}")
    print(f"Mean: {y.mean():.2f}")
    print(f"Std: {y.std():.2f}")
    print(f"Min: {y.min():.2f}")
    print(f"Max: {y.max():.2f}")
    print(f"Range: {y.max() - y.min():.2f}")

    # Coefficient of Variation
    cov = y.std() / y.mean() if y.mean() != 0 else 0
    print(f"\nCoefficient of Variation (CoV): {cov:.4f}")
    print(f"  -> {'MULTIPLICATIVE' if cov > 0.7 else 'ADDITIVE'} mode (threshold=0.7)")

    # Check if all positive
    print(f"\nAll positive: {(y > 0).all()}")
    print(f"Min value: {y.min():.2f}")

    # Trend
    x = np.arange(len(y))
    slope, intercept = np.polyfit(x, y, 1)
    print(f"\nLinear trend:")
    print(f"  Slope: {slope:.4f} per day")
    print(f"  Intercept: {intercept:.2f}")
    print(f"  Final fitted value: {slope * (len(y)-1) + intercept:.2f}")

    # Log space characteristics
    if (y > 0).all():
        log_y = np.log(y)
        print(f"\nLog space characteristics:")
        print(f"  log(y) mean: {log_y.mean():.4f}")
        print(f"  log(y) std: {log_y.std():.4f}")
        print(f"  log(y) min: {log_y.min():.4f}")
        print(f"  log(y) max: {log_y.max():.4f}")
        print(f"  log(y) range: {log_y.max() - log_y.min():.4f}")

        # Log trend
        log_slope, log_intercept = np.polyfit(x, log_y, 1)
        print(f"\nLog space linear trend:")
        print(f"  Slope: {log_slope:.6f} per day")
        print(f"  Intercept: {log_intercept:.4f}")
        log_final = log_slope * (len(y)-1) + log_intercept
        print(f"  Final fitted value (log): {log_final:.4f}")
        print(f"  Final fitted value (exp): {np.exp(log_final):.2f}")

        # Simulate trend projection
        print(f"\nSimulated trend projection in log space:")
        last_two_log = log_y[-2:]
        log_proj_slope = last_two_log[1] - last_two_log[0]
        print(f"  Last 2 log values: {last_two_log}")
        print(f"  Log slope (from last 2): {log_proj_slope:.6f}")

        # Project 14 steps ahead
        for h in [1, 7, 14]:
            log_proj = log_proj_slope * h + last_two_log[1]
            exp_proj = np.exp(log_proj)
            print(f"  Horizon {h:2d}: log={log_proj:7.4f}, exp={exp_proj:12.2f}")

    # Seasonality
    print(f"\nLast 20 values: {y[-20:]}")
    print(f"First 20 values: {y[:20]}")

    return y, cov

def test_anofox_d2139(df):
    """Test Anofox MFLES on D2139 to get actual forecasts."""
    conn = duckdb.connect(database=':memory:')

    # Load extension
    conn.execute("LOAD '/home/simonm/projects/duckdb/anofox-forecast/build/release/extension/anofox/anofox.duckdb_extension'")

    # Create temp table
    conn.register('series_data', df)

    # Run MFLES forecast
    result = conn.execute("""
        SELECT
            unique_id,
            ds,
            yhat
        FROM TS_FORECAST_BY(
            TABLE series_data,
            unique_id,
            ds,
            y,
            'MFLES',
            '{"horizon": 14, "season_length": 7}'
        )
        WHERE ds > (SELECT MAX(ds) FROM series_data)
        ORDER BY ds
    """).fetchdf()

    print("\n" + "="*80)
    print("Anofox MFLES Forecasts for D2139")
    print("="*80)
    forecasts = result['yhat'].values
    print(f"Mean: {forecasts.mean():.2f}")
    print(f"Std: {forecasts.std():.2f}")
    print(f"Min: {forecasts.min():.2f}")
    print(f"Max: {forecasts.max():.2f}")
    print(f"\nAll forecasts: {forecasts}")

    return forecasts

def compare_with_statsforecast(anofox_forecasts):
    """Compare with statsforecast results."""
    print("\n" + "="*80)
    print("Comparison with Statsforecast")
    print("="*80)

    # Load statsforecast results
    df = pd.read_parquet('results/statsforecast-MFLES-Daily.parquet')
    d2139 = df[df['unique_id'] == 'D2139'].sort_values('ds')
    stats_forecasts = d2139['MFLES'].values

    print(f"Statsforecast MFLES forecasts:")
    print(f"  Mean: {stats_forecasts.mean():.2f}")
    print(f"  Std: {stats_forecasts.std():.2f}")
    print(f"  Min: {stats_forecasts.min():.2f}")
    print(f"  Max: {stats_forecasts.max():.2f}")
    print(f"  All forecasts: {stats_forecasts}")

    print(f"\nDifference (Anofox - Statsforecast):")
    diff = anofox_forecasts - stats_forecasts
    print(f"  Mean diff: {diff.mean():.2f}")
    print(f"  Max diff: {diff.max():.2f}")
    print(f"  Mean % diff: {(diff.mean() / stats_forecasts.mean() * 100):.2f}%")

if __name__ == "__main__":
    print("Loading D2139 data...")
    df = load_d2139_data()
    print(f"Loaded {len(df)} observations")

    # Analyze characteristics
    y, cov = analyze_series_characteristics(df)

    # Test Anofox
    anofox_forecasts = test_anofox_d2139(df)

    # Compare with statsforecast
    compare_with_statsforecast(anofox_forecasts)

    # Diagnosis
    print("\n" + "="*80)
    print("DIAGNOSIS")
    print("="*80)
    if cov > 0.7:
        print("✓ D2139 uses MULTIPLICATIVE mode (CoV > 0.7)")
        print("  -> Data is log-transformed before fitting")
        print("  -> Trend is accumulated in log space")
        print("  -> Forecasts are exp() transformed back")
        print("\nPotential issue:")
        print("  If accumulated_trend_ values become too large in log space,")
        print("  exp(large_value) will explode exponentially!")
        print("  e.g., exp(17) = 24 million")
    else:
        print("✗ D2139 uses ADDITIVE mode")
        print("  -> Different issue")
