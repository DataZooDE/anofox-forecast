"""
Baseline models benchmark - configuration-driven wrapper.

Uses shared common modules and configuration files to run the baseline benchmark.
"""
import sys
from pathlib import Path

import fire

# Add benchmark root to sys.path to import shared modules
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from src.common.benchmark_runner import create_benchmark_functions
from configs import baseline, statsforecast_baseline

# Create benchmark functions from configuration
anofox, statsforecast, evaluate, run = create_benchmark_functions(
    anofox_config=baseline,
    statsforecast_config=statsforecast_baseline,
    output_dir=Path(__file__).parent / 'results'
)

if __name__ == '__main__':
    fire.Fire({
        'run': run,
        'anofox': anofox,
        'statsforecast': statsforecast,
        'evaluate': evaluate
    })

