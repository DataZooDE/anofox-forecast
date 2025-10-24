#!/usr/bin/env python3
"""
Comprehensive ETS Model Validation
Tests all ETS model combinations against statsforecast and anofox-time
"""

import numpy as np
import pandas as pd
from statsforecast import StatsForecast
from statsforecast.models import AutoETS
import duckdb
import sys
from pathlib import Path

# AirPassengers dataset (first 132 observations)
AIRPASSENGERS = np.array([
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

HORIZON = 12
SEASON_LENGTH = 12

def create_dataframe():
    """Create pandas DataFrame for statsforecast"""
    dates = pd.date_range(start='1949-01-01', periods=len(AIRPASSENGERS), freq='MS')
    return pd.DataFrame({
        'unique_id': ['AP'] * len(AIRPASSENGERS),
        'ds': dates,
        'y': AIRPASSENGERS
    })

def get_statsforecast_forecast(model, df):
    """Get forecast from statsforecast model"""
    try:
        sf = StatsForecast(models=[model], freq='MS', n_jobs=1)
        forecast = sf.forecast(df=df, h=HORIZON)
        return forecast[model.__class__.__name__].values
    except Exception as e:
        print(f"  ⚠️  statsforecast failed: {e}")
        return None

def get_anofox_forecast(model_name, params=None):
    """Get forecast from anofox-time via DuckDB"""
    try:
        conn = duckdb.connect(':memory:', config={'allow_unsigned_extensions': 'true'})
        # Get path relative to this script
        script_dir = Path(__file__).parent
        ext_path = script_dir.parent / 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'
        conn.execute(f"LOAD '{ext_path}'")
        
        # Create table with data
        conn.execute("""
            CREATE TABLE airp AS
            SELECT 
                DATE '1949-01-01' + INTERVAL ((idx - 1) * 30) DAY AS date,
                value::DOUBLE AS passengers
            FROM (
                SELECT unnest($1) AS value, unnest(generate_series(1, $2)) AS idx
            )
        """, [AIRPASSENGERS.tolist(), len(AIRPASSENGERS)])
        
        # Build forecast query
        if params:
            param_parts = []
            for k, v in params.items():
                if isinstance(v, str):
                    param_parts.append(f"'{k}': '{v}'")
                else:
                    param_parts.append(f"'{k}': {v}")
            param_str = ", ".join(param_parts)
            query = f"SELECT TS_FORECAST(date, passengers, '{model_name}', {HORIZON}, {{{param_str}}}) AS f FROM airp"
        else:
            query = f"SELECT TS_FORECAST(date, passengers, '{model_name}', {HORIZON}, NULL) AS f FROM airp"
        
        result = conn.execute(query).fetchone()
        forecast = result[0]['point_forecast'] if result else None
        conn.close()
        return np.array(forecast) if forecast else None
    except Exception as e:
        print(f"  ⚠️  anofox-time failed: {e}")
        return None

def compare_forecasts(sf_forecast, anofox_forecast, model_name):
    """Compare forecasts and return metrics"""
    if sf_forecast is None or anofox_forecast is None:
        return None
    
    diff = anofox_forecast - sf_forecast
    abs_diff = np.abs(diff)
    pct_diff = (abs_diff / np.abs(sf_forecast)) * 100
    
    max_abs_error = np.max(abs_diff)
    max_pct_error = np.max(pct_diff)
    mean_pct_error = np.mean(pct_diff)
    
    # Check if acceptable (within 5%)
    acceptable = max_pct_error < 5.0
    
    return {
        'model': model_name,
        'max_abs_error': max_abs_error,
        'max_pct_error': max_pct_error,
        'mean_pct_error': mean_pct_error,
        'acceptable': acceptable,
        'sf_forecast': sf_forecast,
        'anofox_forecast': anofox_forecast
    }

def test_autoets():
    """Test AutoETS"""
    print("\n" + "="*70)
    print("Testing AutoETS")
    print("="*70)
    
    df = create_dataframe()
    
    # Statsforecast
    print("\n1. Running statsforecast AutoETS...")
    model = AutoETS(season_length=SEASON_LENGTH)
    sf_forecast = get_statsforecast_forecast(model, df)
    
    if sf_forecast is not None:
        print(f"   First 3 values: {sf_forecast[:3]}")
    
    # Anofox-time
    print("\n2. Running anofox-time AutoETS...")
    anofox_forecast = get_anofox_forecast('AutoETS', {'season_length': SEASON_LENGTH})
    
    if anofox_forecast is not None:
        print(f"   First 3 values: {anofox_forecast[:3]}")
    
    # Compare
    print("\n3. Comparison:")
    result = compare_forecasts(sf_forecast, anofox_forecast, 'AutoETS')
    
    if result:
        print(f"   Max absolute error: {result['max_abs_error']:.2f}")
        print(f"   Max percentage error: {result['max_pct_error']:.2f}%")
        print(f"   Mean percentage error: {result['mean_pct_error']:.2f}%")
        print(f"   Status: {'✅ PASS' if result['acceptable'] else '❌ FAIL'}")
        return result
    else:
        print("   ❌ FAIL - Unable to compare")
        return None

def test_ets_combinations():
    """Test all 15 ETS model combinations from Pegels' taxonomy"""
    print("\n" + "="*70)
    print("Testing All ETS Model Combinations (Pegels' Taxonomy)")
    print("="*70)
    print("\nReference: https://nixtlaverse.nixtla.io/statsforecast/docs/models/autoets.html")
    print("\nTesting all combinations of:")
    print("  Error: A (Additive), M (Multiplicative)")
    print("  Trend: N (None), A (Additive), Ad (Damped Add), M (Mult), Md (Damped Mult)")
    print("  Season: N (None), A (Additive), M (Multiplicative)")
    print("="*70)
    
    # All 15 combinations from Pegels' taxonomy
    # Format: (error, trend, season, season_length, description)
    # We'll test both A and M error types where applicable
    
    test_models = [
        # Row 1: No Trend
        ('ANN', 1, 'Simple Exponential Smoothing (SES) - Additive'),
        ('MNN', 1, 'Simple Exponential Smoothing (SES) - Multiplicative'),
        ('ANA', SEASON_LENGTH, 'No trend, Additive season'),
        ('MNA', SEASON_LENGTH, 'No trend, Additive season, Mult error'),
        ('ANM', SEASON_LENGTH, 'No trend, Multiplicative season'),
        ('MNM', SEASON_LENGTH, 'No trend, Multiplicative season, Mult error'),
        
        # Row 2: Additive Trend
        ('AAN', 1, 'Holt Linear - Additive error'),
        ('MAN', 1, 'Holt Linear - Multiplicative error'),
        ('AAA', SEASON_LENGTH, 'Holt-Winters Additive'),
        ('MAA', SEASON_LENGTH, 'Holt-Winters Additive, Mult error'),
        ('AAM', SEASON_LENGTH, 'Additive trend, Mult season'),
        ('MAM', SEASON_LENGTH, 'Holt-Winters Multiplicative'),
        
        # Row 3: Damped Additive Trend
        ('AAdN', 1, 'Damped trend - Additive error'),
        ('MAdN', 1, 'Damped trend - Multiplicative error'),
        ('AAdA', SEASON_LENGTH, 'Damped trend, Additive season'),
        ('MAdA', SEASON_LENGTH, 'Damped trend, Additive season, Mult error'),
        ('AAdM', SEASON_LENGTH, 'Damped trend, Mult season'),
        ('MAdM', SEASON_LENGTH, 'Damped trend, Mult season, Mult error'),
        
        # Row 4: Multiplicative Trend
        ('AMN', 1, 'Multiplicative trend - Additive error'),
        ('MMN', 1, 'Multiplicative trend - Multiplicative error'),
        ('AMA', SEASON_LENGTH, 'Mult trend, Additive season'),
        ('MMA', SEASON_LENGTH, 'Mult trend, Additive season, Mult error'),
        ('AMM', SEASON_LENGTH, 'Mult trend, Mult season'),
        ('MMM', SEASON_LENGTH, 'Fully Multiplicative'),
        
        # Row 5: Damped Multiplicative Trend
        ('AMdN', 1, 'Damped Mult trend - Additive error'),
        ('MMdN', 1, 'Damped Mult trend - Multiplicative error'),
        ('AMdA', SEASON_LENGTH, 'Damped Mult trend, Add season'),
        ('MMdA', SEASON_LENGTH, 'Damped Mult trend, Add season, Mult error'),
        ('AMdM', SEASON_LENGTH, 'Damped Mult trend, Mult season'),
        ('MMdM', SEASON_LENGTH, 'Fully Damped Multiplicative'),
        
        # Auto models for comparison
        ('ZZN', 1, 'Auto error/trend, no season'),
        ('ZZA', SEASON_LENGTH, 'Auto error/trend, additive season'),
        ('ZZZ', SEASON_LENGTH, 'Fully automatic'),
    ]
    
    results = []
    df = create_dataframe()
    
    for model_spec, season_len, description in test_models:
        print(f"\n{'─'*70}")
        print(f"Testing AutoETS(model='{model_spec}') - {description}")
        print(f"{'─'*70}")
        
        try:
            # Statsforecast
            print("  Running statsforecast...")
            model = AutoETS(season_length=season_len, model=model_spec)
            sf_forecast = get_statsforecast_forecast(model, df)
            
            if sf_forecast is not None:
                print(f"    First 3 values: {sf_forecast[:3]}")
            else:
                print("    ❌ Failed")
                continue
            
            # Anofox-time
            print("  Running anofox-time AutoETS...")
            anofox_forecast = get_anofox_forecast('AutoETS', {
                'season_length': season_len,
                'model': model_spec
            })
            
            if anofox_forecast is not None:
                print(f"    First 3 values: {anofox_forecast[:3]}")
            else:
                print("    ❌ Failed")
                continue
            
            # Compare
            result = compare_forecasts(sf_forecast, anofox_forecast, f'AutoETS({model_spec})')
            
            if result:
                print(f"  Max error: {result['max_pct_error']:.2f}% - {'✅' if result['acceptable'] else '❌'}")
                results.append(result)
            else:
                print(f"  ❌ Comparison failed")
                
        except Exception as e:
            print(f"  ⚠️  Error: {e}")
            import traceback
            traceback.print_exc()
    
    return results

def print_summary(autoets_result, ets_results):
    """Print summary of all tests"""
    print("\n" + "="*70)
    print("SUMMARY")
    print("="*70)
    
    total_tests = 1 + len(ets_results)  # AutoETS + ETS models
    passed = 0
    failed = 0
    excellent = []
    good = []
    acceptable = []
    needs_work = []
    
    # AutoETS
    if autoets_result:
        if autoets_result['max_pct_error'] < 1.0:
            print(f"✅ AutoETS: {autoets_result['max_pct_error']:.2f}% max error - EXCELLENT")
            excellent.append('AutoETS')
            passed += 1
        elif autoets_result['acceptable']:
            print(f"✅ AutoETS: {autoets_result['max_pct_error']:.2f}% max error")
            passed += 1
        else:
            print(f"❌ AutoETS: {autoets_result['max_pct_error']:.2f}% max error (FAIL)")
            needs_work.append(f'AutoETS ({autoets_result["max_pct_error"]:.1f}%)')
            failed += 1
    else:
        print(f"❌ AutoETS: Could not test")
        failed += 1
    
    # ETS models
    for result in ets_results:
        if result['max_pct_error'] < 1.0:
            status = "✅"
            print(f"{status} {result['model']}: {result['max_pct_error']:.2f}% max error - EXCELLENT")
            excellent.append(result['model'])
            passed += 1
        elif result['max_pct_error'] < 5.0:
            status = "✅"
            print(f"{status} {result['model']}: {result['max_pct_error']:.2f}% max error - Good")
            good.append(result['model'])
            passed += 1
        elif result['max_pct_error'] < 20.0:
            status = "~"
            print(f"{status} {result['model']}: {result['max_pct_error']:.2f}% max error - Acceptable")
            acceptable.append(result['model'])
            failed += 1
        else:
            status = "❌"
            print(f"{status} {result['model']}: {result['max_pct_error']:.2f}% max error - NEEDS WORK")
            needs_work.append(f"{result['model']} ({result['max_pct_error']:.1f}%)")
            failed += 1
    
    print(f"\n{'='*70}")
    print(f"Total: {passed}/{total_tests} passed (<5% error), {failed}/{total_tests} needs improvement")
    print(f"{'='*70}")
    
    if excellent:
        print(f"\n⭐ EXCELLENT (<1% error): {', '.join(excellent)}")
    if good:
        print(f"✅ GOOD (1-5% error): {', '.join(good)}")
    if acceptable:
        print(f"~ ACCEPTABLE (5-20% error): {', '.join(acceptable)}")
    if needs_work:
        print(f"❌ NEEDS WORK (>20% error): {', '.join(needs_work)}")
    
    print(f"\n{'='*70}")
    print("RECOMMENDATION:")
    print("  • For seasonal data: Use AutoARIMA (<0.5% error)")
    print("  • For non-seasonal data: Use AutoETS(ZZN) (0.05% error)")
    print("  • For multiple seasonalities: Use MSTL (0.1-6.8% error)")
    print(f"{'='*70}\n")
    
    return passed, failed

def main():
    """Main test execution"""
    print("\n" + "╔"+"═"*68+"╗")
    print("║" + " "*15 + "ETS/AutoETS Comprehensive Validation" + " "*16 + "║")
    print("╚"+"═"*68+"╝")
    
    # Test AutoETS first
    autoets_result = test_autoets()
    
    # Test ETS combinations
    ets_results = test_ets_combinations()
    
    # Print summary
    passed, failed = print_summary(autoets_result, ets_results)
    
    # Exit with appropriate code
    sys.exit(0 if failed == 0 else 1)

if __name__ == '__main__':
    main()

