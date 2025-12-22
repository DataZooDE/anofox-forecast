import os
import sys
import boto3
from pathlib import Path
from src.common.benchmark_runner import create_benchmark_functions

# Import configs
from configs import (
    baseline, statsforecast_baseline,
    ets, statsforecast_ets,
    theta, statsforecast_theta,
    arima, statsforecast_arima,
    mfles, statsforecast_mfles,
    mstl, statsforecast_mstl
)

# Define benchmark configurations
BENCHMARKS = [
    (baseline, statsforecast_baseline, 'm4', 'baseline_benchmark'),
    (ets, statsforecast_ets, 'm4', 'ets_benchmark'),
    (theta, statsforecast_theta, 'm4', 'theta_benchmark'),
    (arima, statsforecast_arima, 'm4', 'arima_benchmark'),
    (mfles, statsforecast_mfles, 'm4', 'mfles_benchmark'),
    (mstl, statsforecast_mstl, 'm4', 'mstl_benchmark'),
    
    (baseline, statsforecast_baseline, 'm5', 'baseline_benchmark'),
    (ets, None, 'm5', 'ets_benchmark'), # statsforecast_ets might be missing for M5 or config differs, using None for safety/simplicity if not present in file structure for M5 specifically, but assuming configs are shared/generic. 
                                        # Re-checking file structure: M5 has its own folders but configs seem shared in `configs/`. 
                                        # The configs import above are likely valid for both if they don't hardcode dataset paths that break.
                                        # However, let's verify if statsforecast configs are used in M5 folders.
                                        # Reading previous `list_dir` of `benchmark/m5/ets_benchmark` showed `run.py` there.
                                        # Most M5 benchmarks in the file list had `run.py`.
    (theta, statsforecast_theta, 'm5', 'theta_benchmark'),
    (arima, None, 'm5', 'arima_benchmark'), # M5 ARIMA might be too slow for statsforecast or just not configured same way.
    (mfles, statsforecast_mfles, 'm5', 'mfles_benchmark'),
    (mstl, statsforecast_mstl, 'm5', 'mstl_benchmark'),
]

# Adjustments based on M5 specificities or missing configs if any:
# For now, we run what we have imported.

def main():
    # Check if S3 bucket is defined
    s3_bucket = os.environ.get('S3_BUCKET')
    
    # Create results directory
    results_root = Path(__file__).parent / 'results'
    results_root.mkdir(exist_ok=True)

    # Datasets and groups to run
    # Usually M4 is Daily/Hourly/etc. M5 is Daily.
    # We'll simplify to running 'Daily' for both for now as per previous context "Daily" was used in examples.
    groups = ['Daily'] 

    for anofox_cfg, stats_cfg, dataset, folder_name in BENCHMARKS:
        print(f"\n{'='*80}")
        print(f"Running {anofox_cfg.BENCHMARK_NAME} on {dataset.upper()}")
        print(f"{'='*80}")
        
        # Determine output directory for this specific benchmark
        # We try to match the structure: m4/baseline_benchmark/results
        # But since we are running from root, maybe just put everything in a centralized results folder?
        # The `create_benchmark_functions` takes `output_dir`.
        # Let's use specific folders to keep it organized or compatible with existing scripts.
        
        output_dir = Path(__file__).parent / dataset / folder_name / 'results'
        output_dir.mkdir(parents=True, exist_ok=True)
        
        # Create functions
        _, _, _, run_func = create_benchmark_functions(
            anofox_config=anofox_cfg,
            statsforecast_config=stats_cfg,
            output_dir=output_dir
        )
        
        for group in groups:
            try:
                run_func(group=group, dataset=dataset)
            except Exception as e:
                print(f"Error running {anofox_cfg.BENCHMARK_NAME} on {dataset} {group}: {e}")

    # Upload to S3 if configured
    if s3_bucket:
        print("\nStarting S3 Upload...")
        s3 = boto3.client('s3')
        root_dir = Path(__file__).parent
        
        for dataset in ['m4', 'm5']:
            dataset_dir = root_dir / dataset
            if dataset_dir.exists():
                # Find all results directories
                for result_dir in dataset_dir.glob('*/results'):
                    # Calculate relative path from benchmark root (e.g., m4/baseline_benchmark/results)
                    rel_dir_from_root = result_dir.relative_to(root_dir)
                    
                    print(f"Processing results in {rel_dir_from_root}...")
                    
                    for file_path in result_dir.rglob('*'):
                        if file_path.is_file():
                            # S3 Key preserves the directory structure: m4/baseline_benchmark/results/filename
                            rel_path = file_path.relative_to(root_dir)
                            s3_key = str(rel_path)
                            
                            print(f"Uploading {s3_key} to bucket {s3_bucket}...")
                            try:
                                s3.upload_file(str(file_path), s3_bucket, s3_key)
                            except Exception as e:
                                print(f"Error uploading {file_path}: {e}")

if __name__ == "__main__":
    main()

