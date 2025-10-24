#!/usr/bin/env python3
"""
Comprehensive MFLES validation against statsforecast
Tests various parameter combinations and seasonal periods
"""

import numpy as np
import duckdb
import sys

# AirPassengers data (132 observations)
airpassengers = np.array([
    112, 118, 132, 129, 121, 135, 148, 148, 136, 119, 104, 118,
    115, 126, 141, 135, 125, 149, 170, 170, 158, 133, 114, 140,
    145, 150, 178, 163, 172, 178, 199, 199, 184, 162, 146, 166,
    171, 180, 193, 181, 183, 218, 230, 242, 209, 191, 172, 194,
    196, 196, 236, 235, 229, 243, 264, 272, 237, 211, 180, 201,
    204, 188, 235, 227, 234, 264, 302, 293, 259, 229, 203, 229,
    242, 233, 267, 269, 270, 315, 364, 347, 312, 274, 237, 278,
    284, 277, 317, 313, 318, 374, 413, 405, 355, 306, 271, 306,
    315, 301, 356, 348, 355, 422, 465, 467, 404, 347, 305, 336,
    340, 318, 362, 348, 363, 435, 491, 505, 404, 359, 310, 337,
    360, 342, 406, 396, 420, 472, 548, 559, 463, 407, 362, 405
], dtype=np.float64)

def get_statsforecast_forecast(data, seasonal_periods, n_iterations=None, **kwargs):
    """Get MFLES forecast from statsforecast"""
    try:
        from statsforecast.models import MFLES
        
        # statsforecast MFLES uses 'season_length' for single period
        # Multiple periods are handled differently
        if isinstance(seasonal_periods, list) and len(seasonal_periods) == 1:
            season_length = seasonal_periods[0]
        elif isinstance(seasonal_periods, list):
            # Multiple seasonalities - not directly supported by MFLES
            print(f"  Note: statsforecast MFLES doesn't support multiple seasonalities")
            return None
        else:
            season_length = seasonal_periods
        
        params = {'season_length': season_length}
        params.update(kwargs)
        
        model = MFLES(**params)
        model.fit(data)
        result = model.predict(h=12)
        return result['mean'][:12]
    except Exception as e:
        print(f"  ⚠️  statsforecast failed: {e}")
        return None

def get_anofox_forecast(data, seasonal_periods, n_iterations=3, lr_trend=0.3, lr_season=0.5, lr_level=0.8):
    """Get MFLES forecast from anofox-time via DuckDB"""
    try:
        conn = duckdb.connect(':memory:', config={'allow_unsigned_extensions': 'true'})
        conn.execute("LOAD '/home/simonm/projects/ai/anofox-forecast/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';")
        
        # Create table
        conn.execute("""
            CREATE TABLE ts AS
            SELECT DATE '2020-01-01' + INTERVAL (idx - 1) DAY AS date,
                   val AS value
            FROM (SELECT UNNEST(?::DOUBLE[]) AS val, UNNEST(generate_series(1, ?)) AS idx)
        """, [data.tolist(), len(data)])
        
        # Forecast with MFLES
        result = conn.execute(f"""
            SELECT TS_FORECAST(date, value, 'MFLES', 12, {{
                'seasonal_periods': {seasonal_periods},
                'n_iterations': {n_iterations},
                'lr_trend': {lr_trend},
                'lr_season': {lr_season},
                'lr_level': {lr_level}
            }})
            FROM ts
        """).fetchone()[0]
        
        return np.array(result['point_forecast'])
    except Exception as e:
        print(f"  ⚠️  anofox-time failed: {e}")
        import traceback
        traceback.print_exc()
        return None

def compare_models(name, data, seasonal_periods, **kwargs):
    """Compare MFLES forecasts between statsforecast and anofox-time"""
    print(f"\n{'='*70}")
    print(f"Testing {name}")
    print(f"  Seasonal periods: {seasonal_periods}")
    print(f"  Parameters: {kwargs}")
    print("="*70)
    
    # Get statsforecast forecast
    print("  Running statsforecast...")
    fc_sf = get_statsforecast_forecast(data, seasonal_periods, **kwargs)
    if fc_sf is not None:
        print(f"    First 3 values: {fc_sf[:3]}")
    
    # Get anofox-time forecast
    print("  Running anofox-time MFLES...")
    fc_anofox = get_anofox_forecast(data, seasonal_periods, **kwargs)
    if fc_anofox is not None:
        print(f"    First 3 values: {fc_anofox[:3]}")
    
    # Compare
    if fc_sf is not None and fc_anofox is not None:
        errors = np.abs((fc_anofox[:3] - fc_sf[:3]) / fc_sf[:3]) * 100
        max_error = errors.max()
        print(f"  Max error: {max_error:.2f}%", end="")
        
        if max_error < 1.0:
            print(" - ✅ EXCELLENT")
            return "excellent"
        elif max_error < 5.0:
            print(" - ✅ GOOD")
            return "good"
        elif max_error < 10.0:
            print(" - ~ ACCEPTABLE")
            return "acceptable"
        else:
            print(" - ❌ NEEDS WORK")
            print(f"\n  Detailed comparison:")
            for i in range(min(3, len(fc_sf))):
                print(f"    Step {i+1}: sf={fc_sf[i]:.2f}, anofox={fc_anofox[i]:.2f}, error={errors[i]:.2f}%")
            return "fail"
    
    return "error"

def main():
    print("\n" + "="*70)
    print("MFLES VALIDATION - Comparing with statsforecast")
    print("="*70)
    
    results = {}
    
    # Test 1: Single seasonality (monthly)
    results['single_season'] = compare_models(
        "MFLES with single seasonality (12)",
        airpassengers,
        seasonal_periods=[12]
    )
    
    # Test 2: Default parameters
    results['default_params'] = compare_models(
        "MFLES with default parameters",
        airpassengers,
        seasonal_periods=[12],
        n_iterations=3
    )
    
    # Test 3: Different learning rates
    results['custom_lr'] = compare_models(
        "MFLES with custom learning rates",
        airpassengers,
        seasonal_periods=[12],
        lr_trend=0.5,
        lr_season=0.3,
        lr_level=0.9
    )
    
    # Test 4: More iterations
    results['more_iters'] = compare_models(
        "MFLES with more iterations (5)",
        airpassengers,
        seasonal_periods=[12],
        n_iterations=5
    )
    
    # Test 5: Multiple seasonalities (if supported)
    # Note: This might not work for statsforecast MFLES
    print(f"\n{'='*70}")
    print("Testing MFLES with multiple seasonalities [7, 12]")
    print("  (statsforecast MFLES might not support this)")
    print("="*70)
    
    fc_anofox = get_anofox_forecast(airpassengers[:84], seasonal_periods=[7, 12])
    if fc_anofox is not None:
        print(f"  anofox-time forecast (first 3): {fc_anofox[:3]}")
        results['multi_season'] = "tested"
    else:
        results['multi_season'] = "failed"
    
    # Summary
    print(f"\n\n{'='*70}")
    print("SUMMARY")
    print("="*70)
    
    excellent = sum(1 for v in results.values() if v == "excellent")
    good = sum(1 for v in results.values() if v == "good")
    acceptable = sum(1 for v in results.values() if v == "acceptable")
    fail = sum(1 for v in results.values() if v == "fail")
    
    print(f"✅ Excellent (<1%): {excellent}")
    print(f"✅ Good (1-5%): {good}")
    print(f"~ Acceptable (5-10%): {acceptable}")
    print(f"❌ Failed (>10%): {fail}")
    
    print(f"\n{'='*70}")
    if excellent + good >= 3:
        print("✅ MFLES implementation validated - production ready!")
    elif excellent + good + acceptable >= 3:
        print("~ MFLES implementation acceptable - may need minor adjustments")
    else:
        print("❌ MFLES implementation needs alignment with statsforecast")
    print("="*70)

if __name__ == "__main__":
    main()

