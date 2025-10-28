import marimo

__generated_with = "0.17.0"
app = marimo.App(width="medium")


@app.cell(hide_code=True)
def _(mo):
    mo.md(r"""# Air Passenger Data""")
    return


@app.cell(hide_code=True)
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


    import altair as alt
    import duckdb
    return AutoARIMA, AutoETS, StatsForecast, alt, duckdb, time


@app.cell
def _(duckdb):
    con = duckdb.connect(config = {"allow_unsigned_extensions": "true"})

    con.sql("""
        INSTALL httpfs;
        LOAD 'httpfs';

        CREATE OR REPLACE TABLE train_data AS
        SELECT * FROM 'https://datasets-nixtla.s3.amazonaws.com/air-passengers.csv'
        WHERE ds < DATE '1960-01-01';

        CREATE OR REPLACE TABLE test_data AS
        SELECT * FROM 'https://datasets-nixtla.s3.amazonaws.com/air-passengers.csv'
        WHERE ds >= DATE '1960-01-01';
    """)
    return (con,)


@app.cell
def _(con):
    con.sql("SELECT unique_id, COUNT(unique_id) AS n FROM train_data GROUP BY unique_id;").df()
    return


@app.cell
def _(con):
    df_train = con.sql("SELECT * FROM train_data;").to_df()
    df_test = con.sql("SELECT * FROM test_data;").to_df()
    return df_test, df_train


@app.cell(hide_code=True)
def _(mo):
    mo.md(
        r"""
    ## Forecasting using ETS(AAA)
    ### `statsforecast`
    """
    )
    return


@app.cell
def _(AutoETS, StatsForecast, df_train, time):
    init = time()
    sf = StatsForecast(
        models=[AutoETS(season_length=12, model='AAA')], 
        freq='MS',
        n_jobs=-1,
    )
    sf.fit(df_train)
    forecast_df_ets = sf.predict(h=12)
    end = time()
    print(f'Minutes taken by StatsForecast on a Laptop: {(end - init) / 60}')
    return (forecast_df_ets,)


@app.cell(hide_code=True)
def _(alt, df_test, df_train, forecast_df_ets):
    # replace _df with your data source
    _chart = (
        alt.Chart(forecast_df_ets)
        .mark_line(color="orange")
        .encode(
            x=alt.X(field='ds', type='temporal', timeUnit='yearmonthdate'),
            y=alt.Y(field='AutoETS', type='quantitative', aggregate='mean'),
            tooltip=[
                alt.Tooltip(field='ds', timeUnit='yearmonthdate', title='forecast_timestamp'),
                alt.Tooltip(field='AutoETS', aggregate='mean', format=',.2f')
            ]
        )
    )

    _ch_train = alt.Chart(df_train).mark_line().encode(
            x=alt.X(field='ds', type='temporal', timeUnit='yearmonthdate'),
            y=alt.Y(field='y', type='quantitative', aggregate='mean'),
    )

    _ch_test = alt.Chart(df_test).mark_line().encode(
            x=alt.X(field='ds', type='temporal', timeUnit='yearmonthdate'),
            y=alt.Y(field='y', type='quantitative', aggregate='mean'),
    )

    _ch_train + _chart + _ch_test
    return


@app.cell(hide_code=True)
def _(mo):
    mo.md(r"""### `anofox-forecast` DuckDB Extenstion""")
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
            FROM TS_FORECAST_BY('train_data', unique_id, ds, y, 'AutoETS', 12, {'seasonal_period': 12,  'error_type': 0, 'trend_type': 1, 'season_type': 1})
        );
    """)
    _end = time()
    print(f'Minutes taken by Anofox-Forecast on a Laptop: {(_end - _init) / 60}')
    return


@app.cell(hide_code=True)
def _(alt, con, df_test, df_train):
    _df = con.sql("""
    SELECT forecast_timestamp AS ds, point_forecast AS yhat FROM ets_forecast ORDER BY unique_id, forecast_step;
    """).to_df()


    # replace _df with your data source
    _chart = (
        alt.Chart(_df)
        .mark_line(color="orange")
        .encode(
            x=alt.X(field='ds', type='temporal', timeUnit='yearmonthdate'),
            y=alt.Y(field='yhat', type='quantitative', aggregate='mean'),
            tooltip=[
                alt.Tooltip(field='ds', timeUnit='yearmonthdate', title='forecast_timestamp'),
                alt.Tooltip(field='yhat', aggregate='mean', format=',.2f')
            ]
        )
    )

    _ch_train = alt.Chart(df_train).mark_line().encode(
            x=alt.X(field='ds', type='temporal', timeUnit='yearmonthdate'),
            y=alt.Y(field='y', type='quantitative', aggregate='mean'),
    )

    _ch_test = alt.Chart(df_test).mark_line().encode(
            x=alt.X(field='ds', type='temporal', timeUnit='yearmonthdate'),
            y=alt.Y(field='y', type='quantitative', aggregate='mean'),
    )

    _ch_train + _chart + _ch_test
    return


@app.cell(hide_code=True)
def _(mo):
    mo.md(
        r"""
    ## Forecasting using AutoARIMA

    ### `statsforecast`
    """
    )
    return


@app.cell
def _(AutoARIMA, StatsForecast, df_train, time):
    _init = time()
    _sf = StatsForecast(
        models=[AutoARIMA(season_length=12)], 
        freq='MS',
        n_jobs=-1,
    )
    _sf.fit(df_train)
    forecast_df_arima = _sf.predict(h=12)
    _end = time()
    print(f'Minutes taken by StatsForecast on a Laptop: {(_end - _init) / 60}')
    return (forecast_df_arima,)


@app.cell(hide_code=True)
def _(alt, df_test, df_train, forecast_df_arima):
    # replace _df with your data source
    _chart = (
        alt.Chart(forecast_df_arima)
        .mark_line(color="orange")
        .encode(
            x=alt.X(field='ds', type='temporal', timeUnit='yearmonthdate'),
            y=alt.Y(field='AutoARIMA', type='quantitative', aggregate='mean'),
            tooltip=[
                alt.Tooltip(field='ds', timeUnit='yearmonthdate', title='forecast_timestamp'),
                alt.Tooltip(field='AutoARIMA', aggregate='mean', format=',.2f')
            ]
        )
    )

    _ch_train = alt.Chart(df_train).mark_line().encode(
            x=alt.X(field='ds', type='temporal', timeUnit='yearmonthdate'),
            y=alt.Y(field='y', type='quantitative', aggregate='mean'),
    )

    _ch_test = alt.Chart(df_test).mark_line().encode(
            x=alt.X(field='ds', type='temporal', timeUnit='yearmonthdate'),
            y=alt.Y(field='y', type='quantitative', aggregate='mean'),
    )

    _ch_train + _chart + _ch_test
    return


@app.cell(hide_code=True)
def _(mo):
    mo.md(r"""### `anofox-forecast` DuckDB Extenstion""")
    return


@app.cell
def _(con, time):
    _init = time()
    con.sql("""
        CREATE OR REPLACE TABLE arima_forecast AS (
    SELECT *
            FROM TS_FORECAST_BY('train_data', unique_id, ds, y, 'AutoARIMA', 12, {'seasonal_period': 12})
        );
    """)
    _end = time()
    print(f'Minutes taken by Anofox-Forecast on a Laptop: {(_end - _init) / 60}')
    return


@app.cell(hide_code=True)
def _(alt, con, df_test, df_train):
    _df = con.sql("""
    SELECT forecast_timestamp AS ds, point_forecast AS yhat FROM arima_forecast ORDER BY unique_id, forecast_step;
    """).to_df()


    # replace _df with your data source
    _chart = (
        alt.Chart(_df)
        .mark_line(color="orange")
        .encode(
            x=alt.X(field='ds', type='temporal', timeUnit='yearmonthdate'),
            y=alt.Y(field='yhat', type='quantitative', aggregate='mean'),
            tooltip=[
                alt.Tooltip(field='ds', timeUnit='yearmonthdate', title='forecast_timestamp'),
                alt.Tooltip(field='yhat', aggregate='mean', format=',.2f')
            ]
        )
    )

    _ch_train = alt.Chart(df_train).mark_line().encode(
            x=alt.X(field='ds', type='temporal', timeUnit='yearmonthdate'),
            y=alt.Y(field='y', type='quantitative', aggregate='mean'),
    )

    _ch_test = alt.Chart(df_test).mark_line().encode(
            x=alt.X(field='ds', type='temporal', timeUnit='yearmonthdate'),
            y=alt.Y(field='y', type='quantitative', aggregate='mean'),
    )

    _ch_train + _chart + _ch_test
    return


if __name__ == "__main__":
    app.run()
