"""
Common benchmark runner factory.

Generates benchmark functions from configuration modules to eliminate code duplication
across all benchmark scripts.
"""
from pathlib import Path
from typing import Optional, Tuple, Callable, Any, Dict

from .data import get_data
from .anofox_runner import run_anofox_benchmark
from .statsforecast_runner import run_statsforecast_benchmark
from .evaluation import evaluate_forecasts


_SUPPORTED_DATASETS: Dict[str, str] = {
    'm4': 'M4',
    'm5': 'M5',
}


def _normalize_dataset(dataset: str) -> Tuple[str, str]:
    key = dataset.lower()
    if key not in _SUPPORTED_DATASETS:
        raise ValueError(
            f"Unsupported dataset '{dataset}'. Supported datasets: {sorted(_SUPPORTED_DATASETS.keys())}"
        )
    return key, _SUPPORTED_DATASETS[key]


def create_benchmark_functions(
    anofox_config: Any,
    statsforecast_config: Optional[Any],
    output_dir: Path,
) -> Tuple[Callable, Optional[Callable], Callable, Callable]:
    """
    Create benchmark functions from configuration modules.

    Parameters
    ----------
    anofox_config : module
        Configuration module for Anofox models. Must have:
        - BENCHMARK_NAME: str
        - MODELS: List[Dict] with 'name' and 'params' keys
    statsforecast_config : module, optional
        Configuration module for Statsforecast models. Must have:
        - BENCHMARK_NAME: str
        - get_models_config(seasonality, horizon): function that returns List[Dict]
        - INCLUDE_PREDICTION_INTERVALS: bool
        Set to None if statsforecast is not supported for this benchmark.
    output_dir : Path
        Directory to save results

    Returns
    -------
    tuple
        (anofox_func, statsforecast_func, evaluate_func, run_func)
        - anofox_func: Function to run anofox benchmarks
        - statsforecast_func: Function to run statsforecast benchmarks (None if not available)
        - evaluate_func: Function to evaluate forecasts
        - run_func: Function to run complete benchmark workflow
    """
    benchmark_name = anofox_config.BENCHMARK_NAME

    def anofox(group: str = 'Daily', dataset: str = 'm4'):
        """
        Run Anofox benchmarks on the selected dataset.

        Parameters
        ----------
        group : str
            Dataset frequency group (e.g., 'Daily', 'Hourly', 'Weekly')
        dataset : str
            Dataset identifier (currently only 'm4')
        """
        dataset_key, dataset_display = _normalize_dataset(dataset)
        print(f"Loading {dataset_display} {group} data for {benchmark_name} benchmark...")
        train_df, horizon, freq, seasonality = get_data(dataset_key, group, train=True)

        # Allow config to cap the number of series (useful for global/panel models
        # where full-panel fitting can be slow). MAX_SERIES=0 means no cap.
        max_series = getattr(anofox_config, 'MAX_SERIES', 0)
        if max_series and max_series > 0:
            all_ids = train_df['unique_id'].unique()
            if len(all_ids) > max_series:
                print(f"Capping to {max_series} series (config MAX_SERIES={max_series}; total={len(all_ids)})")
                selected_ids = all_ids[:max_series]
                train_df = train_df[train_df['unique_id'].isin(selected_ids)].copy()
                print(f"Subset shape: {train_df.shape} ({train_df['unique_id'].nunique()} series)")

        # Allow config to specify the DuckDB function name (e.g., TS_FORECAST_PANEL_BY
        # for global/panel models). Defaults to TS_FORECAST_BY for per-series benchmarks.
        fn_name = getattr(anofox_config, 'FUNCTION_NAME', 'TS_FORECAST_BY')
        run_anofox_benchmark(
            benchmark_name=benchmark_name,
            train_df=train_df,
            horizon=horizon,
            seasonality=seasonality,
            models_config=anofox_config.MODELS,
            output_dir=output_dir,
            group=group,
            freq=freq,
            function_name=fn_name,
        )

    def evaluate(group: str = 'Daily', dataset: str = 'm4'):
        """
        Evaluate model forecasts on the selected dataset.

        Parameters
        ----------
        group : str
            Dataset frequency group
        dataset : str
            Dataset identifier
        """
        # Load test and training data
        dataset_key, dataset_display = _normalize_dataset(dataset)
        print(f"Loading {dataset_display} {group} data for evaluation...")
        test_df, horizon, freq, seasonality = get_data(dataset_key, group, train=False)
        train_df, _, _, _ = get_data(dataset_key, group, train=True)

        evaluate_forecasts(
            benchmark_name=benchmark_name,
            test_df_pd=test_df,
            train_df_pd=train_df,
            seasonality=seasonality,
            results_dir=output_dir,
            group=group
        )

    # Create statsforecast function if config is provided
    statsforecast_func = None
    if statsforecast_config is not None:
        def statsforecast(group: str = 'Daily', dataset: str = 'm4'):
            """
            Run Statsforecast models on the selected dataset.
            
            Parameters
            ----------
            group : str
                Dataset frequency group
            dataset : str
                Dataset identifier
            """
            dataset_key, dataset_display = _normalize_dataset(dataset)
            print(f"Loading {dataset_display} {group} data for {statsforecast_config.BENCHMARK_NAME} benchmark...")
            train_df, horizon, freq, seasonality = get_data(dataset_key, group, train=True)

            # Apply same MAX_SERIES cap as anofox side for fair comparison
            max_series = getattr(anofox_config, 'MAX_SERIES', 0)
            if max_series and max_series > 0:
                all_ids = train_df['unique_id'].unique()
                if len(all_ids) > max_series:
                    print(f"Capping to {max_series} series (matching anofox MAX_SERIES cap)")
                    selected_ids = all_ids[:max_series]
                    train_df = train_df[train_df['unique_id'].isin(selected_ids)].copy()

            # Get models configuration
            models_config = statsforecast_config.get_models_config(seasonality, horizon)

            run_statsforecast_benchmark(
                benchmark_name=statsforecast_config.BENCHMARK_NAME,
                train_df=train_df,
                horizon=horizon,
                freq=freq,
                seasonality=seasonality,
                models_config=models_config,
                output_dir=output_dir,
                group=group,
                include_prediction_intervals=statsforecast_config.INCLUDE_PREDICTION_INTERVALS,
            )
        
        statsforecast_func = statsforecast

    def run(group: str = 'Daily', dataset: str = 'm4'):
        """
        Run complete benchmark: anofox + statsforecast (if available) + evaluation.

        Parameters
        ----------
        group : str
            Dataset frequency group
        dataset : str
            Dataset identifier
        """
        dataset_key, dataset_display = _normalize_dataset(dataset)
        print(f"{'='*80}")
        print(f"{benchmark_name.upper()} BENCHMARK - {dataset_display} {group}")
        print(f"{'='*80}\n")

        step = 1
        
        print(f"STEP {step}: Running Anofox {benchmark_name} models...")
        anofox(group, dataset_key)
        step += 1

        if statsforecast_func is not None:
            print(f"\nSTEP {step}: Running Statsforecast {statsforecast_config.BENCHMARK_NAME} models...")
            statsforecast_func(group, dataset_key)
            step += 1

        print(f"\nSTEP {step}: Evaluating forecasts...")
        evaluate(group, dataset_key)

        print(f"\n{'='*80}")
        print(f"{benchmark_name.upper()} BENCHMARK COMPLETE")
        print(f"{'='*80}")

    return anofox, statsforecast_func, evaluate, run

