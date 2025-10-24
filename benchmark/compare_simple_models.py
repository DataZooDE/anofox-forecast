#!/usr/bin/env python3
"""
Comprehensive comparison of simple/basic forecasting models
between anofox-time (C++) and statsforecast (Python)
"""

import duckdb
import numpy as np
import pandas as pd
from statsforecast import StatsForecast
from statsforecast.models import (
    Naive,
    SeasonalNaive,
    RandomWalkWithDrift,
    WindowAverage,
    SeasonalWindowAverage,
    SimpleExponentialSmoothing,
    SimpleExponentialSmoothingOptimized,
    Holt,
    HoltWinters,
    Theta,
    OptimizedTheta,
    DynamicTheta,
    DynamicOptimizedTheta,
)

# AirPassengers dataset (standard benchmark)
airpassengers = [
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
]

horizon = 12
season_length = 12

print("\n" + "="*70)
print("SIMPLE/BASIC FORECASTING MODELS COMPARISON")
print("="*70)
print(f"\nDataset: AirPassengers ({len(airpassengers)} observations)")
print(f"Forecast horizon: {horizon} months")
print(f"Season length: {season_length}")

# Create DataFrame for statsforecast
df = pd.DataFrame({
    'unique_id': ['series1'] * len(airpassengers),
    'ds': pd.date_range('1949-01-01', periods=len(airpassengers), freq='MS'),
    'y': airpassengers
})

# Setup DuckDB
conn = duckdb.connect(':memory:', config={'allow_unsigned_extensions': 'true'})
conn.execute("LOAD '../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'")
conn.execute("""
    CREATE TABLE airpassengers AS
    SELECT DATE '1949-01-01' + INTERVAL (idx - 1) MONTH AS date,
           val AS passengers
    FROM (
        SELECT UNNEST($data)::DOUBLE AS val,
               UNNEST(generate_series(1, $n)) AS idx
    )
""", {"data": airpassengers, "n": len(airpassengers)})


def compare_model(name, sf_model, ddb_name, params_str="NULL"):
    """Compare a single model between statsforecast and anofox-time"""
    try:
        # statsforecast
        sf = StatsForecast(models=[sf_model], freq='MS', n_jobs=1)
        sf_result = sf.forecast(df=df, h=horizon)
        sf_fc = sf_result[name].values
        
        # anofox-time
        result = conn.execute(f"""
            SELECT TS_FORECAST(date, passengers, '{ddb_name}', {horizon}, {params_str}) AS fc
            FROM airpassengers
        """).fetchone()
        ao_fc = result[0]['point_forecast'][:horizon]
        
        # Compare
        max_error = max(abs(s - a) / s * 100 for s, a in zip(sf_fc, ao_fc) if s > 0.01)
        avg_error = np.mean([abs(s - a) / s * 100 for s, a in zip(sf_fc, ao_fc) if s > 0.01])
        
        status = "✅" if max_error < 5.0 else ("⚠️" if max_error < 10.0 else "❌")
        
        print(f"\n{status} {name:30s} Max: {max_error:6.2f}%  Avg: {avg_error:6.2f}%")
        print(f"   statsforecast: [{sf_fc[0]:.2f}, {sf_fc[1]:.2f}, {sf_fc[2]:.2f}, ...]")
        print(f"   anofox-time:   [{ao_fc[0]:.2f}, {ao_fc[1]:.2f}, {ao_fc[2]:.2f}, ...]")
        
        return max_error
        
    except Exception as e:
        print(f"\n❌ {name:30s} ERROR: {e}")
        return None


# Test all simple models
print("\n" + "="*70)
print("RESULTS:")
print("="*70)

results = {}

# 1. Naive methods
print("\n--- Naive Methods ---")
results['Naive'] = compare_model(
    'Naive', 
    Naive(), 
    'Naive'
)

results['SeasonalNaive'] = compare_model(
    'SeasonalNaive',
    SeasonalNaive(season_length=season_length),
    'SeasonalNaive',
    f"{{'seasonal_period': {season_length}}}"
)

results['RandomWalkWithDrift'] = compare_model(
    'RWD',
    RandomWalkWithDrift(),
    'RandomWalkWithDrift'
)

# 2. Moving Averages
print("\n--- Moving Average Methods ---")
results['SMA'] = compare_model(
    'WindowAverage',
    WindowAverage(window_size=3),
    'SMA',
    "{'window': 3}"  # Our param name is 'window' not 'window_size'
)

# Note: SeasonalWindowAverage exists in our implementation
try:
    results['SeasonalWindowAverage'] = compare_model(
        'SeasonalWindowAverage',
        SeasonalWindowAverage(season_length=season_length, window_size=2),
        'SeasonalWindowAverage',
        f"{{'seasonal_period': {season_length}, 'window': 2}}"
    )
except:
    print("⚠️  SeasonalWindowAverage                Skipped (not in statsforecast results)")
    results['SeasonalWindowAverage'] = None

# 3. Exponential Smoothing
print("\n--- Exponential Smoothing Methods ---")
results['SES'] = compare_model(
    'SES',
    SimpleExponentialSmoothing(alpha=0.5),
    'SES',
    "{'alpha': 0.5}"
)

try:
    results['SESOptimized'] = compare_model(
        'SESOptimized',  # Fixed name
        SimpleExponentialSmoothingOptimized(),
        'SESOptimized'
    )
except:
    print("⚠️  SESOptimized                       Skipped (not in statsforecast results)")
    results['SESOptimized'] = None

# 4. Holt Methods  
print("\n--- Holt Methods ---")
results['Holt'] = compare_model(
    'Holt',
    Holt(),
    'Holt'
)

results['HoltWinters'] = compare_model(
    'HoltWinters',
    HoltWinters(season_length=season_length),
    'HoltWinters',
    f"{{'seasonal_period': {season_length}}}"  # Fixed param name
)

# 5. Theta Methods
print("\n--- Theta Methods ---")
results['Theta'] = compare_model(
    'Theta',
    Theta(season_length=season_length),
    'Theta',
    f"{{'seasonal_period': {season_length}}}"
)

results['OptimizedTheta'] = compare_model(
    'OptimizedTheta',
    OptimizedTheta(season_length=season_length),
    'OptimizedTheta',
    f"{{'seasonal_period': {season_length}}}"
)

results['DynamicTheta'] = compare_model(
    'DynamicTheta',
    DynamicTheta(season_length=season_length),
    'DynamicTheta',
    f"{{'seasonal_period': {season_length}}}"
)

results['DynamicOptimizedTheta'] = compare_model(
    'DynamicOptimizedTheta',
    DynamicOptimizedTheta(season_length=season_length),
    'DynamicOptimizedTheta',
    f"{{'seasonal_period': {season_length}}}"
)

conn.close()

# Summary
print("\n" + "="*70)
print("SUMMARY")
print("="*70)

successful = sum(1 for e in results.values() if e is not None and e < 5.0)
good = sum(1 for e in results.values() if e is not None and e < 10.0)
total = len(results)

print(f"\n✅ {successful}/{total} methods with <5% error")
print(f"⚠️  {good}/{total} methods with <10% error")

if successful == total:
    print("\n🎉 ALL simple models perfectly aligned with statsforecast!")
elif successful >= total * 0.8:
    print("\n👍 Most simple models working well")
else:
    print("\n⚠️  Some methods need attention")

# Best and worst
valid_results = {k: v for k, v in results.items() if v is not None}
if valid_results:
    best = min(valid_results, key=valid_results.get)
    worst = max(valid_results, key=valid_results.get)
    
    print(f"\n📊 Best: {best} ({valid_results[best]:.2f}% error)")
    print(f"📊 Worst: {worst} ({valid_results[worst]:.2f}% error)")

print("\n" + "="*70)

