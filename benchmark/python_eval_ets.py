import pandas as pd
import duckdb
from statsforecast import StatsForecast
from statsforecast.models import AutoETS

df = pd.read_csv('https://datasets-nixtla.s3.amazonaws.com/air-passengers.csv', parse_dates=['ds'])

df_train = df.query("ds < '1960-01-01'")
df_test = df.query("ds >= '1960-01-01'")


# How to use AutoETS with StatsForecast:
# https://nixtlaverse.nixtla.io/statsforecast/docs/models/autoets.html
# sf = StatsForecast(models=[AutoETS(model="AZN")], freq='YS')
sf = StatsForecast(models=[AutoETS(season_length = 12)], freq='MS')
sf.fit(df_train)
forecast_df = sf.predict(h=12)

method_name = AutoETS.__name__

print(forecast_df[method_name].values)


# DuckDB Forecasting
con = duckdb.connect(config = {"allow_unsigned_extensions": "true"})
con.sql("LOAD '../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'")

con.sql("CREATE OR REPLACE TABLE airpassengers AS SELECT * FROM df_train;")

forecast_df_anofox = con.sql("""
    WITH forecasts AS (
    SELECT 
        unique_id,
        TS_FORECAST(ds, y, 'AutoETS', 12, {'seasonal_period': 12, 'n_iterations': 10}) AS f 
    FROM airpassengers GROUP BY unique_id
    )
    SELECT 
        unique_id,
        unnest(f.point_forecast) as forecast,
        unnest(f.lower_95) as lo_95,
        unnest(f.upper_95) as hi_95
    FROM forecasts;
    """).to_df()
forecast_df_anofox["ds"] = forecast_df["ds"]

print(forecast_df_anofox.forecast.values)