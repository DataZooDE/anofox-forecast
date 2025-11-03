"""
Data loading utilities for MFLES benchmark.

Loads M4 competition datasets (Daily, Hourly, Weekly) for benchmarking.
"""
import pandas as pd
from datasetsforecast.m4 import M4


def get_data(directory: str, group: str, train: bool = True):
    """
    Load M4 competition data for benchmarking.

    Parameters
    ----------
    directory : str
        Directory to store/load data (not used with datasetsforecast, but kept for API compatibility)
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    train : bool
        If True, return training data. If False, return test data.

    Returns
    -------
    tuple
        (df, horizon, freq, seasonality) where:
        - df: DataFrame with columns [unique_id, ds, y]
        - horizon: forecast horizon
        - freq: pandas frequency string
        - seasonality: seasonal period
    """
    # M4 dataset configuration
    config = {
        'Daily': {'horizon': 14, 'freq': 'D', 'seasonality': 7},
        'Hourly': {'horizon': 48, 'freq': 'h', 'seasonality': 24},
        'Weekly': {'horizon': 13, 'freq': 'W', 'seasonality': 52},
    }

    if group not in config:
        raise ValueError(f"group must be one of {list(config.keys())}, got {group}")

    # Load M4 data using datasetsforecast
    Y_df, *_ = M4.load(directory='data', group=group)

    # Get config for this group
    horizon = config[group]['horizon']
    freq = config[group]['freq']
    seasonality = config[group]['seasonality']

    # Split train/test
    Y_df_test = Y_df.groupby('unique_id').tail(horizon)
    Y_df_train = Y_df.drop(Y_df_test.index)

    if train:
        return Y_df_train, horizon, freq, seasonality
    else:
        return Y_df_test, horizon, freq, seasonality
