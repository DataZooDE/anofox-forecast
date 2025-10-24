#!/usr/bin/env python3
"""
Debug multiplicative seasonal ETS models by comparing internals
"""

import numpy as np
import duckdb

# AirPassengers data (first 120 observations, for 12-step ahead forecast)
data = np.array([112, 118, 132, 129, 121, 135, 148, 148, 136, 119, 104, 118,
                 115, 126, 141, 135, 125, 149, 170, 170, 158, 133, 114, 140,
                 145, 150, 178, 163, 172, 178, 199, 199, 184, 162, 146, 166,
                 171, 180, 193, 181, 183, 218, 230, 242, 209, 191, 172, 194,
                 196, 196, 236, 235, 229, 243, 264, 272, 237, 211, 180, 201,
                 204, 188, 235, 227, 234, 264, 302, 293, 259, 229, 203, 229,
                 242, 233, 267, 269, 270, 315, 364, 347, 312, 274, 237, 278,
                 284, 277, 317, 313, 318, 374, 413, 405, 355, 306, 271, 306,
                 315, 301, 356, 348, 355, 422, 465, 467, 404, 347, 305, 336,
                 340, 318, 362, 348, 363, 435, 491, 505, 404, 359, 310, 337,
                 360, 342, 406, 396, 420, 472, 548, 559, 463, 407, 362, 405])

print("Testing MNM (Multiplicative error, No trend, Multiplicative season)")
print("=" * 70)

# Test with statsforecast
try:
    from statsforecast.models import AutoETS
    from statsforecast import StatsForecast
    
    sf = StatsForecast(
        models=[AutoETS(season_length=12, model='MNM')],
        freq='MS'
    )
    sf.fit(data)
    fc_sf = sf.forecast(h=12)['AutoETS'].values
    print(f"\nstatsforecast MNM forecast (first 3): {fc_sf[:3]}")
except Exception as e:
    print(f"\nstatsforecast failed: {e}")
    fc_sf = None

# Test with anofox-time
conn = duckdb.connect(':memory:')
conn.execute("LOAD '/home/simonm/projects/ai/anofox-forecast/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';")

# Create table
conn.execute("""
    CREATE TABLE airp AS
    SELECT DATE '1949-01-01' + INTERVAL ((idx - 1) * 30) DAY AS date,
           val AS passengers
    FROM (SELECT UNNEST(?::DOUBLE[]) AS val, UNNEST(generate_series(1, ?)) AS idx)
""", [data.tolist(), len(data)])

# Test MNM
result = conn.execute("""
    SELECT TS_FORECAST(date, passengers, 'AutoETS', 12, {'season_length': 12, 'model': 'MNM'})
    FROM airp
""").fetchone()[0]

fc_anofox = result['point_forecast']
print(f"anofox-time MNM forecast (first 3): {fc_anofox[:3]}")

if fc_sf is not None:
    errors = np.abs((fc_anofox[:3] - fc_sf[:3]) / fc_sf[:3]) * 100
    print(f"\nError: {errors.max():.2f}%")
    print(f"\nDifference breakdown:")
    for i in range(3):
        print(f"  Step {i+1}: sf={fc_sf[i]:.2f}, anofox={fc_anofox[i]:.2f}, error={errors[i]:.2f}%")

print("\n" + "=" * 70)
print("Testing MAM (Multiplicative error, Additive trend, Multiplicative season)")
print("=" * 70)

# Test MAM with statsforecast
try:
    sf = StatsForecast(
        models=[AutoETS(season_length=12, model='MAM')],
        freq='MS'
    )
    sf.fit(data)
    fc_sf = sf.forecast(h=12)['AutoETS'].values
    print(f"\nstatsforecast MAM forecast (first 3): {fc_sf[:3]}")
except Exception as e:
    print(f"\nstatsforecast failed: {e}")
    fc_sf = None

# Test MAM with anofox-time
result = conn.execute("""
    SELECT TS_FORECAST(date, passengers, 'AutoETS', 12, {'season_length': 12, 'model': 'MAM'})
    FROM airp
""").fetchone()[0]

fc_anofox = result['point_forecast']
print(f"anofox-time MAM forecast (first 3): {fc_anofox[:3]}")

if fc_sf is not None:
    errors = np.abs((fc_anofox[:3] - fc_sf[:3]) / fc_sf[:3]) * 100
    print(f"\nError: {errors.max():.2f}%")
    print(f"\nDifference breakdown:")
    for i in range(3):
        print(f"  Step {i+1}: sf={fc_sf[i]:.2f}, anofox={fc_anofox[i]:.2f}, error={errors[i]:.2f}%")

