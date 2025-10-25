import duckdb
import numpy as np
import pandas as pd
from statsforecast import StatsForecast
from statsforecast.models import HoltWinters

data = [112, 118, 132, 129, 121, 135, 148, 148, 136, 119, 104, 118, 115, 126, 141, 135, 125, 149, 170, 170, 158, 133, 114, 140, 145, 150, 178, 163, 172, 178, 199, 199, 184, 162, 146, 166, 171, 180, 193, 181, 183, 218, 230, 242, 209, 191, 172, 194, 196, 196, 236, 235, 229, 243, 264, 272, 237, 211, 180, 201, 204, 188, 235, 227, 234, 264, 302, 293, 259, 229, 203, 229, 242, 233, 267, 269, 270, 315, 364, 347, 312, 274, 237, 278, 284, 277, 317, 313, 318, 374, 413, 405, 355, 306, 271, 306, 315, 301, 356, 348, 355, 422, 465, 467, 404, 347, 305, 336, 340, 318, 362, 348, 363, 435, 491, 505, 404, 359, 310, 337, 360, 342, 406, 396, 420, 472, 548, 559, 463, 407, 362, 405]

df = pd.DataFrame({'unique_id': ['s1']*len(data), 'ds': pd.date_range('1949-01-01', periods=len(data), freq='MS'), 'y': data})
sf = StatsForecast(models=[HoltWinters(season_length=12)], freq='MS', n_jobs=1)
sf_fc = sf.forecast(df=df, h=12)['HoltWinters'].values

conn = duckdb.connect(':memory:', config={'allow_unsigned_extensions': 'true'})
conn.execute('LOAD "../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension"')
conn.execute("""
    CREATE TABLE t AS 
    SELECT DATE '1949-01-01' + INTERVAL (row_number() OVER () - 1) MONTH AS date, val
    FROM (SELECT UNNEST($1)::DOUBLE AS val)
""", [data])
result = conn.execute("SELECT TS_FORECAST(date, val, 'HoltWinters', 12, STRUCT_PACK(seasonal_period := 12)) AS f FROM t").fetchone()
ao_fc = result[0]['point_forecast']

errors = [abs(s-a)/s*100 for s,a in zip(sf_fc, ao_fc)]
print("=" * 70)
print("HOLTWINTERS FINAL VALIDATION")
print("=" * 70)
print(f"\nError Analysis (h=12):")
print(f"  Max error:    {max(errors):.4f}%")
print(f"  Min error:    {min(errors):.4f}%") 
print(f"  Avg error:    {np.mean(errors):.4f}%")
print(f"  Median error: {np.median(errors):.4f}%")
print(f"\n  Errors h=1-6: {[f'{e:.2f}%' for e in errors[:6]]}")

if max(errors) < 1.0:
    print(f"\n✅ SUCCESS: Max error {max(errors):.4f}% < 1%!")
else:
    print(f"\n⚠️  CLOSE: Max error {max(errors):.4f}% (target: <1%)")
    print(f"   Gap to target: {max(errors) - 1.0:.4f}%")

