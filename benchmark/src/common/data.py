"""
Data loading utilities for benchmark datasets.

Currently supports the M4 competition dataset.
"""
from pathlib import Path
from typing import Dict

import pandas as pd
from datasetsforecast.m4 import M4


_DATASETS: Dict[str, Dict] = {
    'm4': {
        'display': 'M4',
        'groups': {
            'Daily': {'horizon': 14, 'freq': 'D', 'seasonality': 7},
            'Hourly': {'horizon': 48, 'freq': 'h', 'seasonality': 24},
            'Weekly': {'horizon': 13, 'freq': 'W', 'seasonality': 52},
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
        Dataset identifier (currently only 'm4').
    group : str
        Frequency group within the dataset (e.g., 'Daily').
    train : bool
        If True, return training data. If False, return test data.

    Returns
    -------
    tuple
        (df, horizon, freq, seasonality)
    """
    dataset_cfg = _validate_dataset(dataset)

    if group not in dataset_cfg['groups']:
        raise ValueError(f"group must be one of {list(dataset_cfg['groups'].keys())}, got {group}")

    # Store/load all datasets in benchmark/data to avoid duplication across benchmarks
    data_root = Path(__file__).resolve().parents[2] / 'data'
    data_root.mkdir(parents=True, exist_ok=True)

    # Load dataset via datasetsforecast helper
    Y_df, *_ = M4.load(directory=str(data_root), group=group)

    cfg = dataset_cfg['groups'][group]
    horizon = cfg['horizon']
    freq = cfg['freq']
    seasonality = cfg['seasonality']

    # Split train/test
    Y_df_test = Y_df.groupby('unique_id').tail(horizon)
    Y_df_train = Y_df.drop(Y_df_test.index)

    if train:
        return Y_df_train, horizon, freq, seasonality
    return Y_df_test, horizon, freq, seasonality
