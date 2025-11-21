"""
Data loading utilities for benchmark datasets.

Currently supports the M4 and M5 competition datasets.
"""
from pathlib import Path
from typing import Dict

import pandas as pd
from datasetsforecast.m4 import M4
from datasetsforecast.m5 import M5


_DATASETS: Dict[str, Dict] = {
    'm4': {
        'display': 'M4',
        'groups': {
            'Daily': {'horizon': 14, 'freq': 'D', 'seasonality': 7},
            'Hourly': {'horizon': 48, 'freq': 'h', 'seasonality': 24},
            'Weekly': {'horizon': 13, 'freq': 'W', 'seasonality': 52},
        },
    },
    'm5': {
        'display': 'M5',
        'groups': {
            'Daily': {'horizon': 28, 'freq': 'D', 'seasonality': 7},
        },
    },
}


def _validate_dataset(dataset: str) -> Dict:
    key = dataset.lower()
    if key not in _DATASETS:
        raise ValueError(f"Unsupported dataset '{dataset}'. Supported datasets: {sorted(_DATASETS)}")
    return _DATASETS[key]


def get_data(dataset: str, group: str, train: bool = True):
    """
    Load benchmark data for the requested dataset/group.

    Parameters
    ----------
    dataset : str
        Dataset identifier ('m4' or 'm5').
    group : str
        Frequency group within the dataset (e.g., 'Daily').
        For M5, this parameter is ignored (M5 only has Daily data).
    train : bool
        If True, return training data. If False, return test data.

    Returns
    -------
    tuple
        (df, horizon, freq, seasonality)
    """
    dataset_cfg = _validate_dataset(dataset)
    dataset_key = dataset.lower()

    if group not in dataset_cfg['groups']:
        raise ValueError(f"group must be one of {list(dataset_cfg['groups'].keys())}, got {group}")

    # Store/load all datasets in benchmark/data to avoid duplication across benchmarks
    data_root = Path(__file__).resolve().parents[2] / 'data'
    data_root.mkdir(parents=True, exist_ok=True)

    # Load dataset via datasetsforecast helper
    if dataset_key == 'm4':
        Y_df, *_ = M4.load(directory=str(data_root), group=group)
    elif dataset_key == 'm5':
        # M5 doesn't have groups parameter, just load the dataset
        Y_df, *_ = M5.load(directory=str(data_root))
    else:
        raise ValueError(f"Unsupported dataset: {dataset}")

    cfg = dataset_cfg['groups'][group]
    horizon = cfg['horizon']
    freq = cfg['freq']
    seasonality = cfg['seasonality']

    # Split train/test
    # M5 uses date-based split (2016-04-25), M4 uses last N observations
    if dataset_key == 'm5':
        # M5 data should have 'ds' column with dates
        # Split based on date threshold: training before 2016-04-25, test from 2016-04-25 onwards
        if 'ds' in Y_df.columns:
            # Convert ds to datetime if it's not already
            if not pd.api.types.is_datetime64_any_dtype(Y_df['ds']):
                Y_df['ds'] = pd.to_datetime(Y_df['ds'])
            split_date = pd.Timestamp('2016-04-25')
            Y_df_train = Y_df[Y_df['ds'] < split_date].copy()
            Y_df_test = Y_df[Y_df['ds'] >= split_date].copy()
        else:
            # Fallback to M4-style split if ds column not found
            Y_df_test = Y_df.groupby('unique_id').tail(horizon)
            Y_df_train = Y_df.drop(Y_df_test.index)
    else:
        # M4-style split: last N observations per series
        Y_df_test = Y_df.groupby('unique_id').tail(horizon)
        Y_df_train = Y_df.drop(Y_df_test.index)

    if train:
        return Y_df_train, horizon, freq, seasonality
    return Y_df_test, horizon, freq, seasonality
