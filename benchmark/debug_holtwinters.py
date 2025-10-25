#!/usr/bin/env python3
"""
Debug HoltWinters to find why forecasts differ by 6.67%
"""

import duckdb
import numpy as np
import pandas as pd
from statsforecast import StatsForecast
from statsforecast.models import HoltWinters

data = [112, 118, 132, 129, 121, 135, 148, 148, 136, 119, 104, 118, 115, 126, 141, 135, 125, 149, 170, 170, 158, 133, 114, 140, 145, 150, 178, 163, 172, 178, 199, 199, 184, 162, 146, 166, 171, 180, 193, 181, 183, 218, 230, 242, 209, 191, 172, 194, 196, 196, 236, 235, 229, 243, 264, 272, 237, 211, 180, 201, 204, 188, 235, 227, 234, 264, 302, 293, 259, 229, 203, 229, 242, 233, 267, 269, 270, 315, 364, 347, 312, 274, 237, 278, 284, 277, 317, 313, 318, 374, 413, 405, 355, 306, 271, 306, 315, 301, 356, 348, 355, 422, 465, 467, 404, 347, 305, 336, 340, 318, 362, 348, 363, 435, 491, 505, 404, 359, 310, 337, 360, 342, 406, 396, 420, 472, 548, 559, 463, 407, 362, 405]

df = pd.DataFrame({
    'unique_id': ['s1'] * len(data),
    'ds': pd.date_range('1949-01-01', periods=len(data), freq='MS'),
    'y': data
})

# statsforecast
print("=" * 70)
print("HOLTWINTERS DEBUG")
print("=" * 70)

model = HoltWinters(season_length=12)
sf = StatsForecast(models=[model], freq='MS', n_jobs=1)
fitted_df = sf.fit(df=df)
fitted_model = fitted_df.fitted_[0,0].model_

print("\nstatsforecast HoltWinters:")
print(f"  Components: {fitted_model['components']}")
print(f"  Alpha: {fitted_model['par'][0]:.6f}")
print(f"  Beta: {fitted_model['par'][1]:.6f}")
print(f"  Gamma: {fitted_model['par'][2]:.6f}")
print(f"  Phi: {fitted_model['par'][3]:.6f}")
print(f"  Final level: {fitted_model['states'][-1, 0]:.4f}")
print(f"  Final trend: {fitted_model['states'][-1, 1]:.4f}")
print(f"  Seasonals (first 3): {fitted_model['states'][-1, 2:5].round(4)}")
print(f"  Seasonals (last 3): {fitted_model['states'][-1, -3:].round(4)}")

sf_fc = sf.forecast(df=df, h=3)['HoltWinters'].values
print(f"  Forecast h=3: [{sf_fc[0]:.2f}, {sf_fc[1]:.2f}, {sf_fc[2]:.2f}]")

# anofox-time
conn = duckdb.connect(':memory:', config={'allow_unsigned_extensions': 'true'})
conn.execute('LOAD "../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension"')
conn.execute('''
    CREATE TABLE t AS 
    SELECT 
        DATE '1949-01-01' + INTERVAL (row_number() OVER () - 1) MONTH AS date,
        val
    FROM (SELECT UNNEST($1)::DOUBLE AS val)
''', [data])

result = conn.execute("SELECT TS_FORECAST(date, val, 'HoltWinters', 3, STRUCT_PACK(seasonal_period := 12)) AS f FROM t").fetchone()
ao_fc = result[0]['point_forecast']

print("\nanofox-time HoltWinters (AutoETS):")
print(f"  Forecast h=3: [{ao_fc[0]:.2f}, {ao_fc[1]:.2f}, {ao_fc[2]:.2f}]")

# Comparison
print("\nComparison:")
for h in range(3):
    err = abs(sf_fc[h] - ao_fc[h]) / sf_fc[h] * 100
    print(f"  h={h+1}: statsforecast={sf_fc[h]:.2f}, anofox={ao_fc[h]:.2f}, error={err:.2f}%")

print("\n" + "=" * 70)

