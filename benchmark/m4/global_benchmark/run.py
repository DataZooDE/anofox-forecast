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
