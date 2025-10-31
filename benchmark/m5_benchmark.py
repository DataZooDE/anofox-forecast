import marimo

__generated_with = "0.17.0"
app = marimo.App(width="medium")


@app.cell
def _():
    import marimo as mo
    return (mo,)


@app.cell
def _():
    from time import time
    import pandas as pd
    import matplotlib

    from statsforecast import StatsForecast
    from statsforecast.models import SeasonalNaive, AutoETS, Theta, AutoARIMA

    import duckdb
    return AutoARIMA, AutoETS, StatsForecast, duckdb, time


@app.cell
def _(duckdb):
    con = duckdb.connect(config = {"allow_unsigned_extensions": "true"})

    con.sql("""
        INSTALL httpfs;
        LOAD 'httpfs';

        CREATE OR REPLACE TABLE train_data AS 
            SELECT 
                item_id AS unique_id, 
                CAST(timestamp AS TIMESTAMP) AS ds, 
                demand AS y 
            FROM 'https://m5-benchmarks.s3.amazonaws.com/data/train/target.parquet'
            WHERE ds < DATE '2016-04-25' AND CONTAINS(unique_id, 'HOUSEHOLD_1') AND CONTAINS(unique_id, 'CA') AND CONTAINS(unique_id, '102')
        ORDER BY unique_id, ds;

        CREATE OR REPLACE TABLE test_data AS 
            SELECT 
                item_id AS unique_id, 
                CAST(timestamp AS TIMESTAMP) AS ds, 
                demand AS y 
            FROM 'https://m5-benchmarks.s3.amazonaws.com/data/train/target.parquet'
            WHERE ds >= DATE '2016-04-25' AND CONTAINS(unique_id, 'HOUSEHOLD_1') AND CONTAINS(unique_id, 'CA') AND CONTAINS(unique_id, '102')
        ORDER BY unique_id, ds;
    """)
    return (con,)


@app.cell
def _(con):
    con.sql("SELECT unique_id, COUNT(unique_id) FROM train_data GROUP BY unique_id;").df()
    return


@app.cell
def _(con):
    con.sql("SELECT * FROM train_data").df()
    return


@app.cell(hide_code=True)
def _(mo):
    mo.md(r"""## ETS(AAA) Forecasting using `statsforecast`""")
    return


@app.cell
def _(con):
    df_train = con.sql("SELECT * FROM train_data;").to_df()
    df_test = con.sql("SELECT * FROM test_data;").to_df()
    return df_test, df_train


@app.cell
def _(AutoETS, StatsForecast, df_train, time):
    init = time()
    sf = StatsForecast(
        models=[AutoETS(season_length=7, model='AAA')], 
        freq='D',
        n_jobs=-1,
    )
    sf.fit(df_train)
    forecast_df_ets = sf.predict(h=28)
    end = time()
    print(f'Minutes taken by StatsForecast on a Laptop: {(end - init) / 60}')
    return (forecast_df_ets,)


@app.cell
def _(AutoARIMA, StatsForecast, df_train, time):
    _init = time()
    _sf = StatsForecast(
        models=[AutoARIMA(season_length=7)], 
        freq='D',
        n_jobs=-1,
    )
    _sf.fit(df_train)
    forecast_df_arima = _sf.predict(h=28)
    _end = time()
    print(f'Minutes taken by StatsForecast on a Laptop: {(_end - _init) / 60}')
    return (forecast_df_arima,)


@app.cell
def _(forecast_df_arima):
    forecast_df_arima
    return


@app.cell(hide_code=True)
def _(mo):
    mo.md(r"""## ETS(AAA) Forecasting using `anofox-forecast` DuckDB Extenstion""")
    return


@app.cell
def _(con):
    con.sql("LOAD '../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'")
    return


@app.cell
def _(con, time):
    _init = time()
    con.sql("""
        CREATE OR REPLACE TABLE ets_forecast AS (
    SELECT *
            FROM TS_FORECAST_BY('train_data', unique_id, ds, y, 'AutoETS', 28, {'seasonal_period': 7,  'error_type': 0, 'trend_type': 1, 'season_type': 1})
        );
    """)
    _end = time()
    print(f'Minutes taken by Anofox-Forecast on a Laptop: {(_end - _init) / 60}')
    return


@app.cell
def _(con, time):
    _init = time()
    con.sql("""
        CREATE OR REPLACE TABLE arima_forecast AS (
    SELECT *
            FROM TS_FORECAST_BY('train_data', unique_id, ds, y, 'AutoARIMA', 28, {'seasonal_period': 7})
        );
    """)
    _end = time()
    print(f'Minutes taken by Anofox-Forecast on a Laptop: {(_end - _init) / 60}')
    return


@app.cell
def _(df_test):
    df_test
    return


@app.cell
def _(forecast_df_ets):
    forecast_df_ets
    return


@app.cell
def _(forecast_df_arima):
    forecast_df_arima
    return


@app.cell
def _():
    return


@app.cell
def _(con):
    con.sql("""
    SELECT * FROM ets_forecast WHERE unique_id = 'HOUSEHOLD_1_102_CA_2' ORDER BY unique_id, forecast_step;
    """).to_df()
    return


@app.cell
def _():
    return


if __name__ == "__main__":
    app.run()
