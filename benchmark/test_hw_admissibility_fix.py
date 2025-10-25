#!/usr/bin/env python3
"""Test HoltWinters with the admissibility constraint fix"""

import duckdb
import numpy as np
from statsforecast import StatsForecast
from statsforecast.models import HoltWinters

# AirPassengers data (first 132 for training, last 12 for validation)
air_passengers = [112, 118, 132, 129, 121, 135, 148, 148, 136, 119, 104, 118, 115, 126, 141, 135, 125, 149, 170, 170, 158, 133, 114, 140, 145, 150, 178, 163, 172, 178, 199, 199, 184, 162, 146, 166, 171, 180, 193, 181, 183, 218, 230, 242, 209, 191, 172, 194, 196, 196, 236, 235, 229, 243, 264, 272, 237, 211, 180, 201, 204, 188, 235, 227, 234, 264, 302, 293, 259, 229, 203, 229, 242, 233, 267, 269, 270, 315, 364, 347, 312, 274, 237, 278, 284, 277, 317, 313, 318, 374, 413, 405, 355, 306, 271, 306, 315, 301, 356, 348, 355, 422, 465, 467, 404, 347, 305, 336, 340, 318, 362, 348, 363, 435, 491, 505, 404, 359, 310, 337, 360, 342, 406, 396, 420, 472, 548, 559, 463, 407, 362, 405, 417, 391, 419, 461, 472, 535, 622, 606, 508, 461, 390, 432]

train_data = air_passengers[:132]
test_data = air_passengers[132:]

print("=" * 80)
print("HOLTWINTERS ADMISSIBILITY FIX VALIDATION")
print("=" * 80)

# statsforecast forecast
import pandas as pd
df = pd.DataFrame({
    'unique_id': ['AP'] * len(train_data),
    'ds': pd.date_range(start='1949-01', periods=len(train_data), freq='MS'),
    'y': train_data
})

sf = StatsForecast(models=[HoltWinters(season_length=12)], freq='MS')
sf_forecast = sf.forecast(df=df, h=12)
sf_values = sf_forecast['HoltWinters'].values

print(f"\nstatsforecast forecast generated successfully")

# anofox-time forecast (uses AutoETS with AAdA model internally)
conn = duckdb.connect(config={'allow_unsigned_extensions': 'true'})
conn.execute("LOAD '../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';")

# Create table with training data
conn.execute("DROP TABLE IF EXISTS air_passengers;")
conn.execute("""
    CREATE TABLE air_passengers AS
    SELECT 
        date '1949-01-01' + INTERVAL (i - 1) MONTH AS date,
        passengers
    FROM (
        SELECT UNNEST(GENERATE_SERIES(1, 132)) AS i,
               UNNEST($1::DOUBLE[]) AS passengers
    )
""", [train_data])

result = conn.execute("""
    SELECT TS_FORECAST(date, passengers, 'HoltWinters', 12, STRUCT_PACK(seasonal_period := 12))
    FROM air_passengers
""").fetchone()

anofox_values = np.array(result[0]['point_forecast'])

print(f"\nstatsforecast forecast: {sf_values[:3]} ...")
print(f"anofox-time forecast:   {anofox_values[:3]} ...")

# Compute errors
errors = np.abs(sf_values - anofox_values) / np.abs(sf_values) * 100
max_error = np.max(errors)
avg_error = np.mean(errors)

print(f"\n{'Step':<6} {'statsforecast':<12} {'anofox-time':<12} {'Error %':<10}")
print("-" * 50)
for i in range(12):
    print(f"{i+1:<6} {sf_values[i]:<12.2f} {anofox_values[i]:<12.2f} {errors[i]:<10.2f}")

print("-" * 50)
print(f"{'Max error:':<30} {max_error:.2f}%")
print(f"{'Average error:':<30} {avg_error:.2f}%")

if max_error < 1.0:
    print("\n🎉 SUCCESS! HoltWinters <1% error achieved!")
elif max_error < 2.0:
    print(f"\n⚠️  Close! Only {max_error - 1.0:.2f}% away from <1% target")
else:
    print(f"\n❌ Gap: {max_error - 1.0:.2f}% to reach <1% target")

print("\n" + "=" * 80)

