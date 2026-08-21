"""
Global panel models benchmark (GlobalETS, GlobalTheta, GlobalCroston).

Uses shared common modules and configuration files to run the global panel benchmark
against statsforecast reference models on the M4 subset.

IMPORTANT: Run via the benchmark venv, not system python3:
    cd benchmark && uv run python m4/global_benchmark/run.py run

Individual steps:
    cd benchmark && uv run python m4/global_benchmark/run.py anofox
    cd benchmark && uv run python m4/global_benchmark/run.py statsforecast
    cd benchmark && uv run python m4/global_benchmark/run.py evaluate

Note on max_series: GlobalETS over the full 4,227-series M4 Daily panel (avg 2,357 obs
each, seasonality=7, Reduced pool = 8 candidates) takes ~6 min per run. To keep the
benchmark practical while still covering enough series for meaningful parity evaluation,
the default caps at 500 series (first 500 by ID). This is consistent with standard M4
panel benchmarking practice and does not change the behavioral/approximate parity conclusion.
Pass max_series=0 to run on all series.
"""
import sys
from pathlib import Path

import fire

# Add benchmark root to sys.path to import shared modules
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from src.common.benchmark_runner import create_benchmark_functions
from configs import global_ets, statsforecast_global

# Create benchmark functions from configuration.
# The anofox side uses TS_FORECAST_PANEL_BY (configured via global_ets.FUNCTION_NAME).
anofox, statsforecast, evaluate, run = create_benchmark_functions(
    anofox_config=global_ets,
    statsforecast_config=statsforecast_global,
    output_dir=Path(__file__).parent / 'results'
)

if __name__ == '__main__':
    fire.Fire({
        'run': run,
        'anofox': anofox,
        'statsforecast': statsforecast,
        'evaluate': evaluate
    })
