# Benchmark Makefile
# Provides targets to run M4 benchmarks and timeseries features pipeline

.PHONY: help m4-benchmark m4-benchmark-baseline m4-benchmark-ets m4-benchmark-theta m4-benchmark-arima m4-benchmark-mfles m4-benchmark-mstl ts-features clean-m4 clean-ts-features

# Default dataset group and dataset
GROUP ?= Daily
DATASET ?= m4

# Benchmark directories
M4_DIR = m4
BASELINE_DIR = $(M4_DIR)/baseline_benchmark
ETS_DIR = $(M4_DIR)/ets_benchmark
THETA_DIR = $(M4_DIR)/theta_benchmark
ARIMA_DIR = $(M4_DIR)/arima_benchmark
MFLES_DIR = $(M4_DIR)/mfles_benchmark
MSTL_DIR = $(M4_DIR)/mstl_benchmark

# Timeseries features directories
TS_FEATURES_DIR = timeseries_features
TS_DATA_DIR = $(TS_FEATURES_DIR)/data

help:
	@echo "Benchmark Makefile"
	@echo ""
	@echo "M4 Benchmark Targets:"
	@echo "  make m4-benchmark              - Run all 6 M4 benchmark types (baseline, ets, theta, arima, mfles, mstl)"
	@echo "  make m4-benchmark-baseline     - Run baseline benchmark only"
	@echo "  make m4-benchmark-ets          - Run ETS benchmark only"
	@echo "  make m4-benchmark-theta        - Run Theta benchmark only"
	@echo "  make m4-benchmark-arima        - Run ARIMA benchmark only"
	@echo "  make m4-benchmark-mfles        - Run MFLES benchmark only"
	@echo "  make m4-benchmark-mstl         - Run MSTL benchmark only"
	@echo ""
	@echo "Timeseries Features Targets:"
	@echo "  make ts-features               - Run complete timeseries features pipeline"
	@echo ""
	@echo "Clean Targets:"
	@echo "  make clean-m4                  - Remove all M4 benchmark results"
	@echo "  make clean-ts-features         - Remove all timeseries features generated files"
	@echo ""
	@echo "Variables:"
	@echo "  GROUP=Daily                    - Dataset group (default: Daily)"
	@echo "  DATASET=m4                     - Dataset identifier (default: m4)"

# Run all M4 benchmarks sequentially
m4-benchmark: m4-benchmark-baseline m4-benchmark-ets m4-benchmark-theta m4-benchmark-arima m4-benchmark-mfles m4-benchmark-mstl
	@echo ""
	@echo "=========================================="
	@echo "All M4 benchmarks completed successfully!"
	@echo "=========================================="

# Individual benchmark targets
m4-benchmark-baseline:
	@echo ""
	@echo "=========================================="
	@echo "Running Baseline Benchmark"
	@echo "=========================================="
	@cd $(BASELINE_DIR) && uv run python run.py anofox $(GROUP) --dataset $(DATASET)
	@cd $(BASELINE_DIR) && uv run python run.py statsforecast $(GROUP) --dataset $(DATASET)
	@cd $(BASELINE_DIR) && uv run python run.py evaluate $(GROUP) --dataset $(DATASET)

m4-benchmark-ets:
	@echo ""
	@echo "=========================================="
	@echo "Running ETS Benchmark"
	@echo "=========================================="
	@cd $(ETS_DIR) && uv run python run.py anofox $(GROUP) --dataset $(DATASET)
	@cd $(ETS_DIR) && uv run python run.py statsforecast $(GROUP) --dataset $(DATASET)
	@cd $(ETS_DIR) && uv run python run.py evaluate $(GROUP) --dataset $(DATASET)

m4-benchmark-theta:
	@echo ""
	@echo "=========================================="
	@echo "Running Theta Benchmark"
	@echo "=========================================="
	@cd $(THETA_DIR) && uv run python run.py anofox $(GROUP) --dataset $(DATASET)
	@cd $(THETA_DIR) && uv run python run_statsforecast.py $(GROUP) --dataset $(DATASET)
	@cd $(THETA_DIR) && uv run python run.py evaluate $(GROUP) --dataset $(DATASET)

m4-benchmark-arima:
	@echo ""
	@echo "=========================================="
	@echo "Running ARIMA Benchmark"
	@echo "=========================================="
	@cd $(ARIMA_DIR) && uv run python run.py anofox $(GROUP) --dataset $(DATASET)
	@cd $(ARIMA_DIR) && uv run python run.py statsforecast $(GROUP) --dataset $(DATASET)
	@cd $(ARIMA_DIR) && uv run python run.py evaluate $(GROUP) --dataset $(DATASET)

m4-benchmark-mfles:
	@echo ""
	@echo "=========================================="
	@echo "Running MFLES Benchmark"
	@echo "=========================================="
	@cd $(MFLES_DIR) && uv run python run.py anofox $(GROUP) --dataset $(DATASET)
	@cd $(MFLES_DIR) && uv run python run.py statsforecast $(GROUP) --dataset $(DATASET)
	@cd $(MFLES_DIR) && uv run python run.py evaluate $(GROUP) --dataset $(DATASET)

m4-benchmark-mstl:
	@echo ""
	@echo "=========================================="
	@echo "Running MSTL Benchmark"
	@echo "=========================================="
	@cd $(MSTL_DIR) && uv run python run.py anofox $(GROUP) --dataset $(DATASET)
	@cd $(MSTL_DIR) && uv run python run.py statsforecast $(GROUP) --dataset $(DATASET)
	@cd $(MSTL_DIR) && uv run python run.py evaluate $(GROUP) --dataset $(DATASET)

# Timeseries features pipeline
ts-features:
	@echo ""
	@echo "=========================================="
	@echo "Running Timeseries Features Pipeline"
	@echo "=========================================="
	@echo "Step 1/4: Generating synthetic data..."
	@cd $(TS_FEATURES_DIR) && duckdb create_data.sql
	@echo "Step 2/4: Computing tsfresh features..."
	@uv run python $(TS_FEATURES_DIR)/tsfresh_features.py \
		--input_path $(TS_DATA_DIR)/time_series_data.parquet \
		--output_path $(TS_DATA_DIR)/tsfresh_results.parquet
	@echo "Step 3/4: Computing anofox features..."
	@cd $(TS_FEATURES_DIR) && duckdb create_anofox_features.sql
	@echo "Step 4/4: Comparing results..."
	@cd $(TS_FEATURES_DIR) && duckdb compare_results.sql
	@echo ""
	@echo "=========================================="
	@echo "Timeseries features pipeline completed!"
	@echo "=========================================="

# Clean targets
clean-m4:
	@echo "Cleaning M4 benchmark results..."
	@rm -rf $(BASELINE_DIR)/results
	@rm -rf $(ETS_DIR)/results
	@rm -rf $(THETA_DIR)/results
	@rm -rf $(ARIMA_DIR)/results
	@rm -rf $(MFLES_DIR)/results
	@rm -rf $(MSTL_DIR)/results
	@echo "M4 benchmark results cleaned."

clean-ts-features:
	@echo "Cleaning timeseries features generated files..."
	@find $(TS_DATA_DIR) -maxdepth 1 -type f \( -name "*.parquet" -o -name "*.csv" \) -not -name "features_overrides*" -delete
	@echo "Timeseries features generated files cleaned."

