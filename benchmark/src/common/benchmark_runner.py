"""
Common benchmark runner factory.

Generates benchmark functions from configuration modules to eliminate code duplication
across all benchmark scripts.
"""
from pathlib import Path
from typing import Optional, Tuple, Callable, Any

from .data import get_data
from .anofox_runner import run_anofox_benchmark
from .statsforecast_runner import run_statsforecast_benchmark
from .evaluation import evaluate_forecasts


def create_benchmark_functions(
    anofox_config: Any,
    statsforecast_config: Optional[Any],
    output_dir: Path
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

    def anofox(group: str = 'Daily'):
        """
        Run Anofox benchmarks on M4 dataset.

        Parameters
        ----------
        group : str
            M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
        """
        print(f"Loading M4 {group} data for {benchmark_name} benchmark...")
        train_df, horizon, freq, seasonality = get_data('data', group, train=True)

        run_anofox_benchmark(
            benchmark_name=benchmark_name,
            train_df=train_df,
            horizon=horizon,
            seasonality=seasonality,
            models_config=anofox_config.MODELS,
            output_dir=output_dir,
            group=group
        )

    def evaluate(group: str = 'Daily'):
        """
        Evaluate model forecasts on M4 test data.

        Parameters
        ----------
        group : str
            M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
        """
        # Load test and training data
        test_df, horizon, freq, seasonality = get_data('data', group, train=False)
        train_df, _, _, _ = get_data('data', group, train=True)

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
        def statsforecast(group: str = 'Daily'):
            """
            Run Statsforecast models on M4 dataset.
            
            Parameters
            ----------
            group : str
                M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
            """
            print(f"Loading M4 {group} data for {statsforecast_config.BENCHMARK_NAME} benchmark...")
            train_df, horizon, freq, seasonality = get_data('data', group, train=True)

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

    def run(group: str = 'Daily'):
        """
        Run complete benchmark: anofox + statsforecast (if available) + evaluation.

        Parameters
        ----------
        group : str
            M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
        """
        print(f"{'='*80}")
        print(f"{benchmark_name.upper()} BENCHMARK - M4 {group}")
        print(f"{'='*80}\n")

        step = 1
        
        print(f"STEP {step}: Running Anofox {benchmark_name} models...")
        anofox(group)
        step += 1

        if statsforecast_func is not None:
            print(f"\nSTEP {step}: Running Statsforecast {statsforecast_config.BENCHMARK_NAME} models...")
            statsforecast_func(group)
            step += 1

        print(f"\nSTEP {step}: Evaluating forecasts...")
        evaluate(group)

        print(f"\n{'='*80}")
        print(f"{benchmark_name.upper()} BENCHMARK COMPLETE")
        print(f"{'='*80}")

    return anofox, statsforecast_func, evaluate, run

