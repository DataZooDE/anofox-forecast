import marimo

__generated_with = "0.17.0"
app = marimo.App(width="medium")


@app.cell
def _():
    import marimo as mo
    return


@app.cell
def _():
    import pandas as pd
    import matplotlib

    from statsforecast import StatsForecast
    from statsforecast.models import AutoARIMA, MSTL, Theta, MFLES, AutoETS

    import duckdb
    import sklearn
    return AutoETS, StatsForecast, duckdb, pd


@app.cell
def _(pd):
    df = pd.read_csv('https://datasets-nixtla.s3.amazonaws.com/air-passengers.csv', parse_dates=['ds'])
    return (df,)


@app.cell
def _(AutoETS, StatsForecast, df):
    sf = StatsForecast(
        models=[AutoETS(season_length = 12)],
        freq='MS',
    )
    sf.fit(df.query("ds < '1960-01-01'"))
    return (sf,)


@app.cell
def _(sf):
    forecast_df = sf.predict(h=12)
    return (forecast_df,)


@app.cell
def _(forecast_df):
    forecast_df
    return


@app.cell
def _(df, forecast_df, sf):
    sf.plot(df, forecast_df)
    return


@app.cell
def _(duckdb):
    con = duckdb.connect(config = {"allow_unsigned_extensions": "true"})
    return (con,)


@app.cell
def _(con):
    con.sql("LOAD '../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'")
    return


@app.cell
def _(con, df):
    df_sub = df.query("ds < '1960-01-01'")
    con.sql("CREATE OR REPLACE TABLE airpassengers AS SELECT * FROM df_sub;")
    return


@app.cell
def _(df):
    df.y.values
    return


@app.cell
def _(con, forecast_df):
    forecast_df_anofox = con.sql("""
    WITH forecasts AS (
    SELECT 
        unique_id,
        TS_FORECAST(ds, y, 'AutoETS', 12, {'seasonal_period': 12, 'n_iterations': 10}) AS f 
    FROM airpassengers GROUP BY unique_id
    )
    SELECT 
        unique_id,
        unnest(f.point_forecast) as AutoARIMA,
        unnest(f.lower_95) as AutoARIMA_lo_95,
        unnest(f.upper_95) as AutoARIMA_hi_95
    FROM forecasts;
    """).to_df()
    forecast_df_anofox["ds"] = forecast_df["ds"]
    return (forecast_df_anofox,)


@app.cell
def _(df, forecast_df_anofox, sf):
    sf.plot(df, forecast_df_anofox)
    return


@app.cell
def _(forecast_df_anofox):
    forecast_df_anofox.AutoARIMA.values
    return


@app.cell
def _():
    return


@app.cell
def _(forecast_df):
    forecast_df["AutoETS"].values
    return


@app.cell
def _():
    return


if __name__ == "__main__":
    app.run()
