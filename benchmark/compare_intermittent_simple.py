#!/usr/bin/env python3
"""
Simple intermittent demand forecasting comparison
"""

import duckdb
import numpy as np
import pandas as pd
from statsforecast import StatsForecast
from statsforecast.models import (
    CrostonClassic,
    CrostonOptimized, 
    CrostonSBA,
    ADIDA,
    IMAPA,
    TSB
)

# Intermittent demand data (sparse with many zeros)
data = [
    0, 0, 5, 0, 3, 0, 0, 7, 0, 0, 2, 0, 0, 0, 4,
    0, 0, 6, 0, 0, 3, 0, 0, 0, 5, 0, 0, 8, 0, 0,
    0, 4, 0, 0, 0, 6, 0, 0, 2, 0, 0, 0, 7, 0, 0,
    3, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 6, 0, 0, 0,
    0, 3, 0, 0, 8, 0, 0, 0, 5, 0, 0, 2, 0, 0, 0,
    7, 0, 0, 4, 0, 0, 0, 6, 0, 0, 3, 0, 0, 0, 0,
    5, 0, 0, 0, 8, 0, 0, 2, 0, 0, 0, 0, 4, 0, 0
]

horizon = 12

print("\n" + "="*70)
print("INTERMITTENT DEMAND FORECASTING COMPARISON")
print("="*70)

zero_pct = sum(1 for x in data if x == 0) / len(data) * 100
print(f"\nDataset: {len(data)} observations, {zero_pct:.1f}% zeros")

# Create DataFrame for statsforecast
df = pd.DataFrame({
    'unique_id': ['series1'] * len(data),
    'ds': pd.date_range('2020-01-01', periods=len(data), freq='D'),
    'y': data
})

# Setup DuckDB
conn = duckdb.connect(':memory:', config={'allow_unsigned_extensions': 'true'})
conn.execute("LOAD '../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'")
conn.execute("""
    CREATE TABLE intermittent_data AS
    SELECT DATE '2020-01-01' + INTERVAL (idx - 1) DAY AS date,
           val AS demand
    FROM (
        SELECT UNNEST($data)::DOUBLE AS val,
               UNNEST(generate_series(1, $n)) AS idx
    )
""", {"data": data, "n": len(data)})

# Test each method
methods = [
    ('CrostonClassic', CrostonClassic(), 'CrostonClassic'),
    ('CrostonOptimized', CrostonOptimized(), 'CrostonOptimized'),
    ('CrostonSBA', CrostonSBA(), 'CrostonSBA'),
    ('ADIDA', ADIDA(), 'ADIDA'),
    ('IMAPA', IMAPA(), 'IMAPA'),
    ('TSB', TSB(alpha_d=0.1, alpha_p=0.1), 'TSB'),
]

print("\nResults:\n")

for display_name, sf_model, ddb_model in methods:
    try:
        # statsforecast
        sf = StatsForecast(models=[sf_model], freq='D', n_jobs=1)
        sf_result = sf.forecast(df=df, h=horizon)
        sf_fc = sf_result[display_name].values
        
        # anofox-time
        result = conn.execute(f"""
            SELECT TS_FORECAST(date, demand, '{ddb_model}', {horizon}, NULL) AS fc
            FROM intermittent_data
        """).fetchone()
        ao_fc = result[0]['point_forecast'][:horizon]
        
        # Compare
        error = max(abs(s - a) / s * 100 for s, a in zip(sf_fc, ao_fc) if s > 0.01)
        status = "✅" if error < 5.0 else "⚠️"
        print(f"{status} {display_name:20s} {error:6.2f}% error")
        print(f"   statsforecast: [{sf_fc[0]:.2f}, {sf_fc[1]:.2f}, {sf_fc[2]:.2f}]")
        print(f"   anofox-time:   [{ao_fc[0]:.2f}, {ao_fc[1]:.2f}, {ao_fc[2]:.2f}]")
        
    except Exception as e:
        print(f"❌ {display_name:20s} ERROR: {e}")

conn.close()

print("\n" + "="*70)

