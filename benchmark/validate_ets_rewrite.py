#!/usr/bin/env python3
"""
Validate ETS rewrite with statsforecast
Using corrected SQL (no cross join!)
"""

import duckdb
import numpy as np
import pandas as pd
from statsforecast import StatsForecast
from statsforecast.models import HoltWinters, Holt

# AirPassengers dataset
data = [112, 118, 132, 129, 121, 135, 148, 148, 136, 119, 104, 118, 115, 126, 141, 135, 125, 149, 170, 170, 158, 133, 114, 140, 145, 150, 178, 163, 172, 178, 199, 199, 184, 162, 146, 166, 171, 180, 193, 181, 183, 218, 230, 242, 209, 191, 172, 194, 196, 196, 236, 235, 229, 243, 264, 272, 237, 211, 180, 201, 204, 188, 235, 227, 234, 264, 302, 293, 259, 229, 203, 229, 242, 233, 267, 269, 270, 315, 364, 347, 312, 274, 237, 278, 284, 277, 317, 313, 318, 374, 413, 405, 355, 306, 271, 306, 315, 301, 356, 348, 355, 422, 465, 467, 404, 347, 305, 336, 340, 318, 362, 348, 363, 435, 491, 505, 404, 359, 310, 337, 360, 342, 406, 396, 420, 472, 548, 559, 463, 407, 362, 405]

df = pd.DataFrame({
    'unique_id': ['s1'] * len(data),
    'ds': pd.date_range('1949-01-01', periods=len(data), freq='MS'),
    'y': data
})

# Setup DuckDB with correct table creation
conn = duckdb.connect(':memory:', config={'allow_unsigned_extensions': 'true'})
conn.execute('LOAD "../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension"')

# Create table with correct SQL (using ROW_NUMBER to avoid cross join)
conn.execute('''
    CREATE TABLE t AS 
    SELECT 
        DATE '1949-01-01' + INTERVAL (row_number() OVER () - 1) MONTH AS date,
        val
    FROM (SELECT UNNEST($1)::DOUBLE AS val)
''', [data])

print("=" * 70)
print("ETS REWRITE VALIDATION")
print("=" * 70)

# Test 1: Holt
print("\n1. Holt (AAN - no seasonality)")
print("-" * 50)
sf = StatsForecast(models=[Holt()], freq='MS', n_jobs=1)
sf_fc = sf.forecast(df=df, h=12)['Holt'].values

result = conn.execute("SELECT TS_FORECAST(date, val, 'Holt', 12, STRUCT_PACK(alpha := 0.9, beta := 0.1)) AS f FROM t").fetchone()
ao_fc = result[0]['point_forecast']

print(f"statsforecast: {sf_fc[:4].round(2)}")
print(f"anofox-time:   {np.array(ao_fc[:4]).round(2)}")
errors = [abs(s-a)/s*100 for s,a in zip(sf_fc, ao_fc) if s != 0]
print(f"Max error: {max(errors):.2f}%")
print(f"Avg error: {np.mean(errors):.2f}%")

# Test 2: HoltWinters
print("\n2. HoltWinters (AAA with damping)")
print("-" * 50)
sf = StatsForecast(models=[HoltWinters(season_length=12)], freq='MS', n_jobs=1)
sf_fc = sf.forecast(df=df, h=12)['HoltWinters'].values

result = conn.execute("SELECT TS_FORECAST(date, val, 'HoltWinters', 12, STRUCT_PACK(seasonal_period := 12)) AS f FROM t").fetchone()
ao_fc = result[0]['point_forecast']

print(f"statsforecast: {sf_fc[:4].round(2)}")
print(f"anofox-time:   {np.array(ao_fc[:4]).round(2)}")
errors = [abs(s-a)/s*100 for s,a in zip(sf_fc, ao_fc) if s != 0]
print(f"Max error: {max(errors):.2f}%")
print(f"Avg error: {np.mean(errors):.2f}%")

print("\n" + "=" * 70)
print("VALIDATION COMPLETE")
print("=" * 70)

