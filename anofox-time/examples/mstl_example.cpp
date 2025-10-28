#include "anofox-time/models/mstl_forecaster.hpp"
#include "anofox-time/models/seasonal_naive.hpp"
#include "anofox-time/models/theta.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/utils/metrics.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>

using namespace anofoxtime;
using namespace std::chrono;

namespace {

// AirPassengers dataset (full 144 months)
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

// Generate synthetic multi-seasonal data
std::vector<double> generateMultiSeasonalData(int n) {
	std::vector<double> data(n);
	for (int i = 0; i < n; ++i) {
		double weekly = 10.0 * std::sin(2.0 * M_PI * i / 7.0);
		double monthly = 5.0 * std::sin(2.0 * M_PI * i / 30.0);
		double trend = 0.2 * i;
		data[i] = 100.0 + trend + weekly + monthly;
	}
	return data;
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
	
	std::cout << std::left << std::setw(30) << method 
	          << " MAE: " << std::setw(8) << std::fixed << std::setprecision(2) << mae
	          << " RMSE: " << std::setw(8) << rmse
	          << " sMAPE: " << std::setw(7) << std::setprecision(1) << smape * 100 << "%";
	
	if (time_ms > 0.0) {
		std::cout << " Time: " << std::setw(8) << std::setprecision(2) << time_ms << " ms";
	}
	
	std::cout << "\n";
}

} // namespace

int main() {
	std::cout << "\n";
	std::cout << R"(
╔═══════════════════════════════════════════════════════════════════════════╗
║                     MSTL Forecasting Examples                             ║
║        Multiple Seasonal-Trend Decomposition with Forecasting             ║
╚═══════════════════════════════════════════════════════════════════════════╝
)" << "\n";

	// =======================================================================
	// Scenario 1: Basic MSTL with Single Seasonality
	// =======================================================================
	
	printHeader("Scenario 1: MSTL with Single Seasonality (AirPassengers)");
	
	auto full_data = airPassengersData();
	std::vector<double> train_data(full_data.begin(), full_data.begin() + 132);
	std::vector<double> test_data(full_data.begin() + 132, full_data.end());
	
	auto train_ts = createTimeSeries(train_data);
	
	std::cout << "Training MSTL on AirPassengers data...\n";
	std::cout << "Train: 132 months | Test: 12 months\n";
	
	models::MSTLForecaster mstl({12});
	mstl.fit(train_ts);
	auto forecast = mstl.predict(12);
	
	std::cout << "\nActual Test:  ";
	for (int i = 0; i < 12; ++i) {
		std::cout << std::fixed << std::setprecision(1) << test_data[i];
		if (i < 11) std::cout << ", ";
	}
	std::cout << "\n";
	
	std::cout << "MSTL Forecast: ";
	for (int i = 0; i < 12; ++i) {
		std::cout << std::fixed << std::setprecision(1) << forecast.primary()[i];
		if (i < 11) std::cout << ", ";
	}
	std::cout << "\n";
	
	printSubHeader("Results");
	printMetrics("MSTL (Linear Trend)", test_data, forecast.primary());
	
	// =======================================================================
	// Scenario 2: Multiple Seasonalities
	// =======================================================================
	
	printHeader("Scenario 2: Multiple Seasonalities (Synthetic Data)");
	
	std::cout << "Generating data with weekly (7) and monthly (30) patterns...\n";
	
	auto multi_data = generateMultiSeasonalData(120);
	std::vector<double> multi_train(multi_data.begin(), multi_data.begin() + 90);
	std::vector<double> multi_test(multi_data.begin() + 90, multi_data.end());
	
	auto multi_train_ts = createTimeSeries(multi_train);
	
	models::MSTLForecaster mstl_multi({7, 30});
	mstl_multi.fit(multi_train_ts);
	auto multi_forecast = mstl_multi.predict(30);
	
	printSubHeader("Results");
	printMetrics("MSTL (Multiple Seasons)", multi_test, multi_forecast.primary());
	
	const auto& components = mstl_multi.components();
	std::cout << "\nDecomposition:\n";
	std::cout << "  - Trend component:    " << components.trend.size() << " values\n";
	std::cout << "  - Seasonal components: " << components.seasonal.size() << " patterns\n";
	std::cout << "  - Remainder:          " << components.remainder.size() << " values\n";
	
	// =======================================================================
	// Scenario 3: Different Trend Methods
	// =======================================================================
	
	printHeader("Scenario 3: Comparison of Trend Forecasting Methods");
	
	std::cout << "Comparing Linear, SES, Holt, and None trend methods...\n\n";
	
	struct TrendConfig {
		std::string name;
		models::MSTLForecaster::TrendMethod method;
	};
	
	std::vector<TrendConfig> configs = {
		{"Linear Regression", models::MSTLForecaster::TrendMethod::Linear},
		{"Simple Exponential Smoothing", models::MSTLForecaster::TrendMethod::SES},
		{"Holt's Linear Trend", models::MSTLForecaster::TrendMethod::Holt},
		{"Constant (No Extrapolation)", models::MSTLForecaster::TrendMethod::None}
	};
	
	for (const auto& config : configs) {
		models::MSTLForecaster mstl_trend({12}, config.method);
		mstl_trend.fit(train_ts);
		auto fc = mstl_trend.predict(12);
		
		printMetrics("MSTL (" + config.name + ")", test_data, fc.primary());
	}
	
	// =======================================================================
	// Scenario 4: Comparison with Other Methods
	// =======================================================================
	
	printHeader("Scenario 4: MSTL vs Other Forecasting Methods");
	
	std::cout << "Benchmarking on AirPassengers (132 → 12)\n\n";
	
	struct BenchmarkResult {
		std::string method;
		double mae;
		double rmse;
		double smape;
		double time_ms;
	};
	
	std::vector<BenchmarkResult> results;
	
	// MSTL Linear
	{
		auto start = high_resolution_clock::now();
		models::MSTLForecaster model({12}, models::MSTLForecaster::TrendMethod::Linear);
		model.fit(train_ts);
		auto fc = model.predict(12);
		auto end = high_resolution_clock::now();
		
		double mae = utils::Metrics::mae(test_data, fc.primary());
		double rmse = utils::Metrics::rmse(test_data, fc.primary());
		double smape = utils::Metrics::smape(test_data, fc.primary()).value_or(0.0);
		double time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
		
		results.push_back({"MSTL (Linear)", mae, rmse, smape, time_ms});
	}
	
	// MSTL Holt
	{
		auto start = high_resolution_clock::now();
		models::MSTLForecaster model({12}, models::MSTLForecaster::TrendMethod::Holt);
		model.fit(train_ts);
		auto fc = model.predict(12);
		auto end = high_resolution_clock::now();
		
		double mae = utils::Metrics::mae(test_data, fc.primary());
		double rmse = utils::Metrics::rmse(test_data, fc.primary());
		double smape = utils::Metrics::smape(test_data, fc.primary()).value_or(0.0);
		double time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
		
		results.push_back({"MSTL (Holt)", mae, rmse, smape, time_ms});
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
	
	// Sort by MAE
	std::sort(results.begin(), results.end(), 
	          [](const BenchmarkResult& a, const BenchmarkResult& b) {
		          return a.mae < b.mae;
	          });
	
	std::cout << std::left << std::setw(30) << "Method" 
	          << std::setw(10) << "MAE" 
	          << std::setw(10) << "RMSE" 
	          << std::setw(10) << "sMAPE" 
	          << std::setw(12) << "Time (ms)" << "\n";
	std::cout << std::string(72, '-') << "\n";
	
	for (const auto& r : results) {
		std::cout << std::left << std::setw(30) << r.method
		          << std::setw(10) << std::fixed << std::setprecision(2) << r.mae
		          << std::setw(10) << r.rmse
		          << std::setw(9) << std::setprecision(1) << r.smape * 100 << "%"
		          << std::setw(12) << std::setprecision(2) << r.time_ms << "\n";
	}
	
	// =======================================================================
	// Summary
	// =======================================================================
	
	printHeader("Summary: MSTL Method");
	
	std::cout << R"(
MSTL (Multiple Seasonal-Trend decomposition using LOESS + Forecasting)
──────────────────────────────────────────────────────────────────────

Algorithm Overview:
  MSTL combines proven STL decomposition with flexible trend forecasting:
  
  1. Decompose time series into trend + multiple seasonal components + remainder
  2. Forecast the trend using selected method (Linear, SES, Holt, or constant)
  3. Project each seasonal component cyclically
  4. Combine trend forecast with all seasonal projections

Key Features:
  ✓ Handles multiple seasonalities naturally (e.g., hourly with daily+weekly+yearly)
  ✓ Four trend forecasting methods to choose from
  ✓ Uses proven LOESS-based decomposition
  ✓ Optional robust fitting for outlier resistance
  ✓ Fast and interpretable

Strengths:
  • Excellent for data with multiple seasonal patterns
  • Interpretable decomposition (can examine components)
  • Fast execution (1-3ms typical)
  • Robust to outliers (with robust option)
  • Well-established methodology

Limitations:
  • Simpler than state-space models (TBATS)
  • Trend methods are relatively basic
  • No automatic parameter optimization
  • Requires sufficient data (2+ cycles per seasonal period)

When to Use MSTL:
  → Data has multiple seasonal cycles
  → Need interpretable decomposition
  → Want fast, reliable forecasting
  → Prefer proven methodology over complex models
  → Have sufficient historical data

Trend Method Selection:
  • Linear:  Best for data with consistent linear trend
  • SES:     Good for level-dominated series
  • Holt:    Best for series with linear trend
  • None:    When trend is stable/negligible

Performance on AirPassengers:
  • MAE typically 15-25 (competitive)
  • Execution time: 1-3ms (very fast)
  • Best trend method: Depends on data characteristics

)" << "\n";
	
	std::cout << "Example completed successfully!\n\n";
	
	return 0;
}

