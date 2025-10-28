#include "anofox-time/models/tbats.hpp"
#include "anofox-time/models/auto_tbats.hpp"
#include "anofox-time/models/mstl_forecaster.hpp"
#include "anofox-time/models/theta.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/utils/metrics.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>

using namespace anofoxtime;
using namespace std::chrono;

namespace {

// AirPassengers dataset
std::vector<double> airPassengersData() {
	return {
		112., 118., 132., 129., 121., 135., 148., 148., 136., 119., 104., 118.,
		115., 126., 141., 135., 125., 149., 170., 170., 158., 133., 114., 140.,
		145., 150., 178., 163., 172., 178., 199., 199., 184., 162., 146., 166.,
		171., 180., 193., 181., 183., 218., 230., 242., 209., 191., 172., 194.,
		196., 196., 236., 235., 229., 243., 264., 272., 237., 211., 180., 201.,
		204., 188., 235., 227., 234., 264., 302., 293., 259., 229., 203., 229.,
		242., 233., 267., 269., 270., 315., 364., 347., 312., 274., 237., 278.,
		284., 277., 317., 313., 318., 374., 413., 405., 355., 306., 271., 306.,
		315., 301., 356., 348., 355., 422., 465., 467., 404., 347., 305., 336.,
		340., 318., 362., 348., 363., 435., 491., 505., 404., 359., 310., 337.,
		360., 342., 406., 396., 420., 472., 548., 559., 463., 407., 362., 405.,
		417., 391., 419., 461., 472., 535., 622., 606., 508., 461., 390., 432.
	};
}

core::TimeSeries createTimeSeries(const std::vector<double>& data) {
	std::vector<core::TimeSeries::TimePoint> timestamps;
	timestamps.reserve(data.size());
	auto start = core::TimeSeries::TimePoint{};
	for (std::size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	return core::TimeSeries(std::move(timestamps), data);
}

void printHeader(const std::string& title) {
	std::cout << "\n" << std::string(80, '=') << "\n";
	std::cout << title << "\n";
	std::cout << std::string(80, '=') << "\n\n";
}

void printMetrics(const std::string& method, 
                  const std::vector<double>& actual,
                  const std::vector<double>& forecast,
                  double time_ms = 0.0) {
	double mae = utils::Metrics::mae(actual, forecast);
	double rmse = utils::Metrics::rmse(actual, forecast);
	auto smape_opt = utils::Metrics::smape(actual, forecast);
	double smape = smape_opt.value_or(0.0);
	
	std::cout << std::left << std::setw(30) << method 
	          << " MAE: " << std::setw(8) << std::fixed << std::setprecision(2) << mae
	          << " RMSE: " << std::setw(8) << rmse
	          << " sMAPE: " << std::setw(7) << std::setprecision(1) << smape * 100 << "%";
	
	if (time_ms > 0.0) {
		std::cout << " Time: " << std::setw(10) << std::setprecision(2) << time_ms << " ms";
	}
	
	std::cout << "\n";
}

} // namespace

int main() {
	std::cout << "\n";
	std::cout << R"(
╔═══════════════════════════════════════════════════════════════════════════╗
║                     TBATS Forecasting Examples                            ║
║      Trigonometric, Box-Cox, ARMA, Trend, Seasonal State-Space Model     ║
╚═══════════════════════════════════════════════════════════════════════════╝
)" << "\n";

	// Prepare data
	auto full_data = airPassengersData();
	std::vector<double> train_data(full_data.begin(), full_data.begin() + 132);
	std::vector<double> test_data(full_data.begin() + 132, full_data.end());
	
	auto train_ts = createTimeSeries(train_data);
	
	// =======================================================================
	// Scenario 1: Basic TBATS
	// =======================================================================
	
	printHeader("Scenario 1: Basic TBATS Configuration");
	
	std::cout << "Testing different TBATS configurations on AirPassengers...\n";
	std::cout << "Train: 132 months | Test: 12 months\n\n";
	
	struct ConfigTest {
		std::string name;
		models::TBATS::Config config;
	};
	
	std::vector<ConfigTest> configs;
	
	// Basic config
	models::TBATS::Config config1;
	config1.seasonal_periods = {12};
	configs.push_back({"TBATS (basic)", config1});
	
	// With Box-Cox
	models::TBATS::Config config2;
	config2.seasonal_periods = {12};
	config2.use_box_cox = true;
	config2.box_cox_lambda = 0.0;  // Log
	configs.push_back({"TBATS (Box-Cox log)", config2});
	
	// With damped trend
	models::TBATS::Config config3;
	config3.seasonal_periods = {12};
	config3.use_trend = true;
	config3.use_damped_trend = true;
	config3.damping_param = 0.98;
	configs.push_back({"TBATS (damped trend)", config3});
	
	// With ARMA
	models::TBATS::Config config4;
	config4.seasonal_periods = {12};
	config4.ar_order = 1;
	config4.ma_order = 1;
	configs.push_back({"TBATS (ARMA 1,1)", config4});
	
	for (const auto& cfg : configs) {
		models::TBATS tbats(cfg.config);
		tbats.fit(train_ts);
		auto fc = tbats.predict(12);
		
		printMetrics(cfg.name, test_data, fc.primary());
	}
	
	// =======================================================================
	// Scenario 2: AutoTBATS - Automatic Optimization
	// =======================================================================
	
	printHeader("Scenario 2: AutoTBATS - Automatic Parameter Selection");
	
	std::cout << "AutoTBATS automatically tests multiple configurations...\n\n";
	
	auto start = high_resolution_clock::now();
	models::AutoTBATS auto_tbats({12});
	auto_tbats.fit(train_ts);
	auto forecast_auto = auto_tbats.predict(12);
	auto end = high_resolution_clock::now();
	
	double time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
	
	printMetrics("AutoTBATS", test_data, forecast_auto.primary(), time_ms);
	
	std::cout << "\nOptimization Details:\n";
	const auto& diag = auto_tbats.diagnostics();
	std::cout << "  Models evaluated: " << diag.models_evaluated << "\n";
	std::cout << "  Best AIC: " << std::fixed << std::setprecision(2) << diag.best_aic << "\n";
	std::cout << "  Optimization time: " << diag.optimization_time_ms << " ms\n";
	
	const auto& best_config = auto_tbats.selectedConfig();
	std::cout << "\nSelected Configuration:\n";
	std::cout << "  Box-Cox: " << (best_config.use_box_cox ? "Yes" : "No");
	if (best_config.use_box_cox) {
		std::cout << " (λ=" << best_config.box_cox_lambda << ")";
	}
	std::cout << "\n";
	std::cout << "  Trend: " << (best_config.use_trend ? "Yes" : "No");
	if (best_config.use_trend && best_config.use_damped_trend) {
		std::cout << " (damped, φ=" << best_config.damping_param << ")";
	}
	std::cout << "\n";
	std::cout << "  ARMA: AR(" << best_config.ar_order << "), MA(" << best_config.ma_order << ")\n";
	std::cout << "  Fourier K: ";
	for (size_t i = 0; i < best_config.fourier_k.size(); ++i) {
		std::cout << best_config.fourier_k[i];
		if (i < best_config.fourier_k.size() - 1) std::cout << ", ";
	}
	std::cout << "\n";
	
	// =======================================================================
	// Scenario 3: Comparison with Other Methods
	// =======================================================================
	
	printHeader("Scenario 3: TBATS vs Other Forecasting Methods");
	
	std::cout << "Benchmarking on AirPassengers (132 → 12)\n\n";
	
	struct BenchmarkResult {
		std::string method;
		double mae;
		double rmse;
		double time_ms;
	};
	
	std::vector<BenchmarkResult> results;
	
	// AutoTBATS (already fitted above)
	{
		double mae = utils::Metrics::mae(test_data, forecast_auto.primary());
		double rmse = utils::Metrics::rmse(test_data, forecast_auto.primary());
		results.push_back({"AutoTBATS", mae, rmse, time_ms});
	}
	
	// TBATS basic
	{
		auto start = high_resolution_clock::now();
		models::TBATS::Config config;
		config.seasonal_periods = {12};
		models::TBATS model(config);
		model.fit(train_ts);
		auto fc = model.predict(12);
		auto end = high_resolution_clock::now();
		
		double mae = utils::Metrics::mae(test_data, fc.primary());
		double rmse = utils::Metrics::rmse(test_data, fc.primary());
		double t = duration_cast<microseconds>(end - start).count() / 1000.0;
		results.push_back({"TBATS (basic)", mae, rmse, t});
	}
	
	// MSTL
	{
		auto start = high_resolution_clock::now();
		models::MSTLForecaster model({12});
		model.fit(train_ts);
		auto fc = model.predict(12);
		auto end = high_resolution_clock::now();
		
		double mae = utils::Metrics::mae(test_data, fc.primary());
		double rmse = utils::Metrics::rmse(test_data, fc.primary());
		double t = duration_cast<microseconds>(end - start).count() / 1000.0;
		results.push_back({"MSTL (Linear)", mae, rmse, t});
	}
	
	// Theta
	{
		auto start = high_resolution_clock::now();
		models::Theta model(12);
		model.fit(train_ts);
		auto fc = model.predict(12);
		auto end = high_resolution_clock::now();
		
		double mae = utils::Metrics::mae(test_data, fc.primary());
		double rmse = utils::Metrics::rmse(test_data, fc.primary());
		double t = duration_cast<microseconds>(end - start).count() / 1000.0;
		results.push_back({"Theta", mae, rmse, t});
	}
	
	// Sort by MAE
	std::sort(results.begin(), results.end(), 
	          [](const BenchmarkResult& a, const BenchmarkResult& b) {
		          return a.mae < b.mae;
	          });
	
	std::cout << std::left << std::setw(30) << "Method" 
	          << std::setw(10) << "MAE" 
	          << std::setw(10) << "RMSE" 
	          << std::setw(12) << "Time (ms)" << "\n";
	std::cout << std::string(62, '-') << "\n";
	
	for (const auto& r : results) {
		std::cout << std::left << std::setw(30) << r.method
		          << std::setw(10) << std::fixed << std::setprecision(2) << r.mae
		          << std::setw(10) << r.rmse
		          << std::setw(12) << std::setprecision(2) << r.time_ms << "\n";
	}
	
	// =======================================================================
	// Summary
	// =======================================================================
	
	printHeader("Summary: TBATS Method");
	
	std::cout << R"(
TBATS (Trigonometric, Box-Cox, ARMA errors, Trend, Seasonal)
──────────────────────────────────────────────────────────────────────

Algorithm Overview:
  TBATS is an innovations state-space model for time series with
  multiple seasonalities. It combines:
  
  1. Trigonometric (Fourier) representation for seasonal patterns
  2. Optional Box-Cox transformation for variance stabilization
  3. Optional ARMA errors for autocorrelation modeling
  4. Trend component (with optional damping)
  5. State-space framework for robust parameter estimation

Key Features:
  ✓ Handles multiple seasonalities naturally
  ✓ Box-Cox transformation for heteroscedasticity
  ✓ Fourier terms provide smooth seasonal patterns
  ✓ State-space framework ensures consistency
  ✓ Optional ARMA for complex error structures

Strengths:
  • Excellent for data with multiple seasonal patterns
  • Robust to variance changes (via Box-Cox)
  • Smooth, stable forecasts
  • Well-established methodology
  • AutoTBATS provides automatic configuration

Limitations:
  • More complex than simpler methods
  • Slower than MSTL or Theta
  • Requires sufficient data
  • AutoTBATS can be slow (tests many configurations)

When to Use TBATS:
  → Data has multiple seasonal cycles
  → Variance changes over time (use Box-Cox)
  → Need robust, smooth forecasts
  → Willing to accept longer training time
  → Want state-space framework guarantees

When to Use AutoTBATS:
  → Don't know optimal configuration
  → Need automatic model selection
  → Willing to wait for optimization
  → Want best possible TBATS model

Performance on AirPassengers:
  • Competitive accuracy with established methods
  • Slower than MFLES/MSTL but more sophisticated
  • AutoTBATS finds optimal configuration automatically

Configuration Options:
  • use_box_cox: Enable variance stabilization
  • box_cox_lambda: 0=log, 0.5=sqrt, 1=none
  • use_trend: Include trend component
  • use_damped_trend: Dampen long-term trend
  • ar_order, ma_order: ARMA error modeling
  • fourier_k: Auto-selected or manual

)" << "\n";
	
	std::cout << "Example completed successfully!\n\n";
	
	return 0;
}

