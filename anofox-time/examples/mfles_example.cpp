#include "anofox-time/models/mfles.hpp"
#include "anofox-time/models/theta.hpp"
#include "anofox-time/models/arima.hpp"
#include "anofox-time/models/seasonal_naive.hpp"
#include "anofox-time/models/auto_ets.hpp"
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

// AirPassengers dataset (full 144 months, 1949-1960)
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

void printSubHeader(const std::string& title) {
	std::cout << "\n" << title << "\n";
	std::cout << std::string(title.length(), '-') << "\n";
}

void printMetrics(const std::string& method, 
                  const std::vector<double>& actual,
                  const std::vector<double>& forecast,
                  double time_ms = 0.0) {
	double mae = utils::Metrics::mae(actual, forecast);
	double rmse = utils::Metrics::rmse(actual, forecast);
	auto smape_opt = utils::Metrics::smape(actual, forecast);
	double smape = smape_opt.value_or(0.0);
	
	std::cout << std::left << std::setw(25) << method 
	          << " MAE: " << std::setw(8) << std::fixed << std::setprecision(2) << mae
	          << " RMSE: " << std::setw(8) << rmse
	          << " sMAPE: " << std::setw(7) << std::setprecision(1) << smape * 100 << "%";
	
	if (time_ms > 0.0) {
		std::cout << " Time: " << std::setw(8) << std::setprecision(2) << time_ms << " ms";
	}
	
	std::cout << "\n";
}

void printForecast(const std::string& label, const std::vector<double>& forecast, int max_print = 12) {
	std::cout << label << ": ";
	for (int i = 0; i < std::min(max_print, static_cast<int>(forecast.size())); ++i) {
		std::cout << std::fixed << std::setprecision(1) << forecast[i];
		if (i < max_print - 1 && i < static_cast<int>(forecast.size()) - 1) {
			std::cout << ", ";
		}
	}
	std::cout << "\n";
}

} // namespace

int main() {
	std::cout << "\n";
	std::cout << R"(
╔═══════════════════════════════════════════════════════════════════════════╗
║                       MFLES Forecasting Examples                          ║
║         Multiple Seasonalities Fourier-based Exponential Smoothing        ║
╚═══════════════════════════════════════════════════════════════════════════╝
)" << "\n";

	// =======================================================================
	// Scenario 1: Basic MFLES with Single Seasonality
	// =======================================================================
	
	printHeader("Scenario 1: Basic MFLES with Single Seasonality (Period = 12)");
	
	std::cout << "Using AirPassengers data (1949-1960)\n";
	std::cout << "Train: First 132 months | Test: Last 12 months\n";
	
	auto full_data = airPassengersData();
	std::vector<double> train_data(full_data.begin(), full_data.begin() + 132);
	std::vector<double> test_data(full_data.begin() + 132, full_data.end());
	
	auto train_ts = createTimeSeries(train_data);
	
	std::cout << "\nTraining MFLES(period=12, iterations=3)...\n";
	
	models::MFLES mfles({12});  // Monthly seasonality
	mfles.fit(train_ts);
	auto forecast = mfles.predict(12);
	
	std::cout << "\nModel Parameters:\n";
	std::cout << "  Seasonal Periods: {12}\n";
	std::cout << "  Iterations: " << mfles.iterations() << "\n";
	std::cout << "  Trend LR: " << mfles.trendLearningRate() << "\n";
	std::cout << "  Seasonal LR: " << mfles.seasonalLearningRate() << "\n";
	std::cout << "  Level LR: " << mfles.levelLearningRate() << "\n";
	
	printSubHeader("Results");
	std::cout << "Actual Test:  ";
	for (int i = 0; i < 12; ++i) {
		std::cout << std::fixed << std::setprecision(1) << test_data[i];
		if (i < 11) std::cout << ", ";
	}
	std::cout << "\n";
	
	printForecast("MFLES Forecast", forecast.primary(), 12);
	
	std::cout << "\nAccuracy Metrics:\n";
	printMetrics("MFLES (period=12)", test_data, forecast.primary());
	
	// =======================================================================
	// Scenario 2: Custom Learning Rates
	// =======================================================================
	
	printHeader("Scenario 2: MFLES with Custom Learning Rates");
	
	std::cout << "Comparing different learning rate configurations...\n";
	
	struct LRConfig {
		std::string name;
		double lr_trend;
		double lr_season;
		double lr_level;
	};
	
	std::vector<LRConfig> configs = {
		{"Default (0.3, 0.5, 0.8)", 0.3, 0.5, 0.8},
		{"Trend-focused (0.8, 0.3, 0.3)", 0.8, 0.3, 0.3},
		{"Season-focused (0.2, 0.9, 0.3)", 0.2, 0.9, 0.3},
		{"Balanced (0.5, 0.5, 0.5)", 0.5, 0.5, 0.5},
		{"Conservative (0.1, 0.2, 0.3)", 0.1, 0.2, 0.3}
	};
	
	std::cout << "\n";
	for (const auto& config : configs) {
		models::MFLES mfles_custom({12}, 3, config.lr_trend, config.lr_season, config.lr_level);
		mfles_custom.fit(train_ts);
		auto fc = mfles_custom.predict(12);
		
		printMetrics(config.name, test_data, fc.primary());
	}
	
	// =======================================================================
	// Scenario 3: Multiple Seasonalities
	// =======================================================================
	
	printHeader("Scenario 3: Multiple Seasonalities");
	
	std::cout << "AirPassengers has both quarterly (4) and yearly (12) patterns.\n";
	std::cout << "Testing MFLES with multiple seasonal periods...\n\n";
	
	struct MultiSeasonConfig {
		std::string name;
		std::vector<int> periods;
	};
	
	std::vector<MultiSeasonConfig> multi_configs = {
		{"Single: 12-month", {12}},
		{"Single: 4-quarter", {4}},
		{"Dual: 4 + 12", {4, 12}},
		{"Dual: 12 + 4 (order)", {12, 4}}
	};
	
	for (const auto& config : multi_configs) {
		models::MFLES mfles_multi(config.periods);
		mfles_multi.fit(train_ts);
		auto fc = mfles_multi.predict(12);
		
		printMetrics(config.name, test_data, fc.primary());
	}
	
	// =======================================================================
	// Scenario 4: Varying Iterations
	// =======================================================================
	
	printHeader("Scenario 4: Effect of Boosting Iterations");
	
	std::cout << "Testing how the number of gradient boosting iterations affects accuracy...\n\n";
	
	for (int iter = 1; iter <= 7; ++iter) {
		models::MFLES mfles_iter({12}, iter);
		mfles_iter.fit(train_ts);
		auto fc = mfles_iter.predict(12);
		
		std::string name = "Iterations = " + std::to_string(iter);
		printMetrics(name, test_data, fc.primary());
	}
	
	// =======================================================================
	// Scenario 5: Comparison with Other Methods
	// =======================================================================
	
	printHeader("Scenario 5: MFLES vs Other Forecasting Methods");
	
	std::cout << "Benchmarking MFLES against other methods on AirPassengers...\n";
	std::cout << "Train: 132 months → Test: 12 months\n\n";
	
	struct BenchmarkResult {
		std::string method;
		double mae;
		double rmse;
		double smape;
		double time_ms;
	};
	
	std::vector<BenchmarkResult> results;
	
	// MFLES
	{
		auto start = high_resolution_clock::now();
		models::MFLES model({12}, 3);
		model.fit(train_ts);
		auto fc = model.predict(12);
		auto end = high_resolution_clock::now();
		
		double mae = utils::Metrics::mae(test_data, fc.primary());
		double rmse = utils::Metrics::rmse(test_data, fc.primary());
		double smape = utils::Metrics::smape(test_data, fc.primary()).value_or(0.0);
		double time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
		
		results.push_back({"MFLES (default)", mae, rmse, smape, time_ms});
	}
	
	// MFLES with multiple periods
	{
		auto start = high_resolution_clock::now();
		models::MFLES model({4, 12}, 3);
		model.fit(train_ts);
		auto fc = model.predict(12);
		auto end = high_resolution_clock::now();
		
		double mae = utils::Metrics::mae(test_data, fc.primary());
		double rmse = utils::Metrics::rmse(test_data, fc.primary());
		double smape = utils::Metrics::smape(test_data, fc.primary()).value_or(0.0);
		double time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
		
		results.push_back({"MFLES (multi-season)", mae, rmse, smape, time_ms});
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
		double smape = utils::Metrics::smape(test_data, fc.primary()).value_or(0.0);
		double time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
		
		results.push_back({"Theta", mae, rmse, smape, time_ms});
	}
	
	// Seasonal Naive
	{
		auto start = high_resolution_clock::now();
		models::SeasonalNaive model(12);
		model.fit(train_ts);
		auto fc = model.predict(12);
		auto end = high_resolution_clock::now();
		
		double mae = utils::Metrics::mae(test_data, fc.primary());
		double rmse = utils::Metrics::rmse(test_data, fc.primary());
		double smape = utils::Metrics::smape(test_data, fc.primary()).value_or(0.0);
		double time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
		
		results.push_back({"Seasonal Naive", mae, rmse, smape, time_ms});
	}
	
	// AutoETS
	{
		auto start = high_resolution_clock::now();
		models::AutoETS model(12, "ZZZ");  // Automatic selection
		model.fit(train_ts);
		auto fc = model.predict(12);
		auto end = high_resolution_clock::now();
		
		double mae = utils::Metrics::mae(test_data, fc.primary());
		double rmse = utils::Metrics::rmse(test_data, fc.primary());
		double smape = utils::Metrics::smape(test_data, fc.primary()).value_or(0.0);
		double time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
		
		results.push_back({"AutoETS", mae, rmse, smape, time_ms});
	}
	
	// Sort by MAE
	std::sort(results.begin(), results.end(), 
	          [](const BenchmarkResult& a, const BenchmarkResult& b) {
		          return a.mae < b.mae;
	          });
	
	std::cout << std::left << std::setw(25) << "Method" 
	          << std::setw(10) << "MAE" 
	          << std::setw(10) << "RMSE" 
	          << std::setw(10) << "sMAPE" 
	          << std::setw(12) << "Time (ms)" << "\n";
	std::cout << std::string(67, '-') << "\n";
	
	for (const auto& r : results) {
		std::cout << std::left << std::setw(25) << r.method
		          << std::setw(10) << std::fixed << std::setprecision(2) << r.mae
		          << std::setw(10) << r.rmse
		          << std::setw(9) << std::setprecision(1) << r.smape * 100 << "%"
		          << std::setw(12) << std::setprecision(2) << r.time_ms << "\n";
	}
	
	// =======================================================================
	// Summary
	// =======================================================================
	
	printHeader("Summary: MFLES Method");
	
	std::cout << R"(
MFLES (Multiple seasonalities Fourier-based Exponential Smoothing)
───────────────────────────────────────────────────────────────────

Algorithm Overview:
  MFLES uses gradient boosted time series decomposition to model complex
  patterns. It iteratively fits three components on residuals:
  
  1. Linear Trend (with learning rate lr_trend)
  2. Fourier Seasonality for multiple periods (with learning rate lr_season)
  3. Exponential Smoothing Level (with learning rate lr_level)
  
  The Fourier representation uses K sin/cos pairs to capture seasonal
  patterns, where K = min(period/2, 10) to balance complexity.

Key Features:
  ✓ Handles multiple seasonalities naturally (e.g., weekly + yearly)
  ✓ Fast training (no optimization required)
  ✓ Interpretable components (trend, season, level)
  ✓ Configurable learning rates for each component
  ✓ Gradient boosting iterations improve fit

Strengths:
  • Excellent for data with multiple seasonal patterns
  • Very fast compared to AutoARIMA or AutoETS
  • Smooth forecasts via Fourier representation
  • Interpretable decomposition into components
  • Stable and robust

Limitations:
  • Assumes linear trend (not exponential)
  • Fixed learning rates (not optimized per dataset)
  • Simpler than state-space models like ETS
  • May underfit if iterations too low

Performance on AirPassengers:
  • Competitive accuracy with established methods
  • Sub-millisecond training time
  • Best suited when multiple seasonalities present
  • Works well with moderate seasonal strength

When to Use MFLES:
  → Data has multiple seasonal cycles (e.g., hourly with daily+weekly patterns)
  → Need fast forecasting at scale
  → Want interpretable component decomposition
  → Prefer stable, smooth forecasts
  → Linear trend assumption is reasonable

Default Parameters:
  • seasonal_periods: {12} (customize based on data)
  • n_iterations: 3 (increase for better fit, diminishing returns after 5-7)
  • lr_trend: 0.3 (increase if strong trend)
  • lr_season: 0.5 (increase if strong seasonality)
  • lr_level: 0.8 (increase if level dominates)

)" << "\n";
	
	std::cout << "Example completed successfully!\n\n";
	
	return 0;
}

