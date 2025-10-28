#include "anofox-time/models/auto_arima.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/utils/metrics.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace anofoxtime;

namespace {

std::vector<double> generateARProcess(std::size_t length, double phi = 0.7) {
	std::mt19937 rng(42);
	std::normal_distribution<double> noise(0.0, 1.0);
	
	std::vector<double> data;
	data.reserve(length);
	data.push_back(10.0);
	
	for (std::size_t i = 1; i < length; ++i) {
		data.push_back(phi * data.back() + noise(rng));
	}
	
	return data;
}

std::vector<double> generateTrendingSeries(std::size_t length) {
	std::mt19937 rng(42);
	std::normal_distribution<double> noise(0.0, 2.0);
	
	std::vector<double> data;
	data.reserve(length);
	
	for (std::size_t i = 0; i < length; ++i) {
		const double trend = 0.5 * static_cast<double>(i);
		data.push_back(100.0 + trend + noise(rng));
	}
	
	return data;
}

std::vector<double> generateSeasonalSeries(std::size_t length, int period = 12) {
	std::mt19937 rng(42);
	std::normal_distribution<double> noise(0.0, 3.0);
	
	std::vector<double> data;
	data.reserve(length);
	
	for (std::size_t i = 0; i < length; ++i) {
		const double seasonal = 15.0 * std::sin(2.0 * M_PI * static_cast<double>(i % period) / static_cast<double>(period));
		const double trend = 0.2 * static_cast<double>(i);
		data.push_back(120.0 + trend + seasonal + noise(rng));
	}
	
	return data;
}

std::vector<double> airPassengersData() {
	return {
		112., 118., 132., 129., 121., 135., 148., 148., 136., 119., 104., 118.,
		115., 126., 141., 135., 125., 149., 170., 170., 158., 133., 114., 140.,
		145., 150., 178., 163., 172., 178., 199., 199., 184., 162., 146., 166.,
		171., 180., 193., 181., 183., 218., 230., 242., 209., 191., 172., 194.
	};
}

struct Scenario {
	std::string name;
	std::vector<double> history;
	std::vector<double> actual;
	int seasonal_period;
};

Scenario buildScenario(const std::string &name, const std::vector<double> &full_data, 
                       std::size_t history_size, int seasonal_period = 0) {
	Scenario scenario;
	scenario.name = name;
	scenario.seasonal_period = seasonal_period;
	scenario.history.assign(full_data.begin(), full_data.begin() + history_size);
	scenario.actual.assign(full_data.begin() + history_size, full_data.end());
	return scenario;
}

core::TimeSeries createTimeSeries(const std::vector<double> &data) {
	std::vector<core::TimeSeries::TimePoint> timestamps;
	timestamps.reserve(data.size());
	auto start = core::TimeSeries::TimePoint{};
	for (std::size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	return core::TimeSeries(std::move(timestamps), data);
}

void printModelInfo(const models::AutoARIMA &model) {
	const auto &comp = model.components();
	
	std::cout << "  Selected model: ARIMA(" << comp.p << "," << comp.d << "," << comp.q << ")";
	if (comp.seasonal_period > 0) {
		std::cout << "(" << comp.P << "," << comp.D << "," << comp.Q << ")[" << comp.seasonal_period << "]";
	}
	std::cout << '\n';
	
	if (comp.include_drift) {
		std::cout << "  Includes drift term\n";
	}
	if (comp.include_constant) {
		std::cout << "  Includes constant term\n";
	}
}

void printMetrics(const models::AutoARIMAMetrics &metrics) {
	std::cout << "  Model Metrics:\n";
	if (std::isfinite(metrics.aicc)) {
		std::cout << "    AICc:  " << std::fixed << std::setprecision(2) << metrics.aicc << '\n';
	}
	if (std::isfinite(metrics.aic)) {
		std::cout << "    AIC:   " << std::fixed << std::setprecision(2) << metrics.aic << '\n';
	}
	if (std::isfinite(metrics.bic)) {
		std::cout << "    BIC:   " << std::fixed << std::setprecision(2) << metrics.bic << '\n';
	}
	if (std::isfinite(metrics.sigma2)) {
		std::cout << "    Sigma²: " << std::fixed << std::setprecision(4) << metrics.sigma2 << '\n';
	}
	std::cout.unsetf(std::ios::floatfield);
}

void printDiagnostics(const models::AutoARIMADiagnostics &diag) {
	std::cout << "  Diagnostics:\n";
	std::cout << "    Models evaluated: " << diag.models_evaluated << '\n';
	std::cout << "    Models failed:    " << diag.models_failed << '\n';
	std::cout << "    Training size:    " << diag.training_data_size << '\n';
	std::cout << "    Search mode:      " << (diag.stepwise_used ? "Stepwise" : "Exhaustive") << '\n';
}

void printForecast(const core::Forecast &forecast, std::size_t preview_count = 5) {
	const auto &pred = forecast.primary();
	if (pred.empty()) {
		return;
	}
	
	const std::size_t count = std::min(preview_count, pred.size());
	std::cout << "  Forecast (first " << count << " points): ";
	for (std::size_t i = 0; i < count; ++i) {
		std::cout << std::fixed << std::setprecision(2) << pred[i];
		if (i + 1 < count) std::cout << ", ";
	}
	std::cout << '\n';
	std::cout.unsetf(std::ios::floatfield);
}

void printAccuracyMetrics(const std::vector<double> &actual, const std::vector<double> &forecast) {
	if (actual.size() != forecast.size() || actual.empty()) {
		return;
	}
	
	const double mae = utils::Metrics::mae(actual, forecast);
	const double rmse = utils::Metrics::rmse(actual, forecast);
	const auto smape_opt = utils::Metrics::smape(actual, forecast);
	const auto r2_opt = utils::Metrics::r2(actual, forecast);
	
	std::cout << "  Forecast Accuracy:\n";
	if (std::isfinite(mae)) {
		std::cout << "    MAE:   " << std::fixed << std::setprecision(4) << mae << '\n';
	}
	if (std::isfinite(rmse)) {
		std::cout << "    RMSE:  " << std::fixed << std::setprecision(4) << rmse << '\n';
	}
	if (smape_opt && std::isfinite(*smape_opt)) {
		std::cout << "    sMAPE: " << std::fixed << std::setprecision(4) << *smape_opt << '\n';
	}
	if (r2_opt && std::isfinite(*r2_opt)) {
		std::cout << "    R²:    " << std::fixed << std::setprecision(4) << *r2_opt << '\n';
	}
	std::cout.unsetf(std::ios::floatfield);
}

void runScenario(const Scenario &scenario) {
	std::cout << "\n=== " << scenario.name << " ===\n";
	std::cout << "Training data: " << scenario.history.size() << " points\n";
	std::cout << "Forecast horizon: " << scenario.actual.size() << " points\n";
	
	// Create time series
	auto ts = createTimeSeries(scenario.history);
	
	// Configure AutoARIMA
	models::AutoARIMA auto_arima(scenario.seasonal_period);
	auto_arima
		.setMaxP(5)
		.setMaxQ(5)
		.setMaxD(2)
		.setStepwise(true)
		.setInformationCriterion(models::AutoARIMA::InformationCriterion::AICc)
		.setAllowDrift(true);
	
	// Enable seasonal components for SARIMA model selection
	if (scenario.seasonal_period > 0) {
		auto_arima.setMaxSeasonalP(2).setMaxSeasonalD(1).setMaxSeasonalQ(2);
	}
	
	// Fit model
	auto_arima.fit(ts);
	
	// Print model info
	printModelInfo(auto_arima);
	printMetrics(auto_arima.metrics());
	printDiagnostics(auto_arima.diagnostics());
	
	// Generate forecast
	const int horizon = static_cast<int>(scenario.actual.size());
	auto forecast = auto_arima.predict(horizon);
	printForecast(forecast, 5);
	
	// Evaluate accuracy
	printAccuracyMetrics(scenario.actual, forecast.primary());
	
	// Show confidence intervals
	std::cout << "\n  95% Confidence Intervals:\n";
	auto forecast_ci = auto_arima.predictWithConfidence(horizon, 0.95);
	for (std::size_t i = 0; i < std::min<std::size_t>(3, forecast_ci.primary().size()); ++i) {
		std::cout << "    Step " << (i+1) << ": " 
		          << std::fixed << std::setprecision(2) 
		          << forecast_ci.primary()[i] << " ["
		          << forecast_ci.lowerSeries()[i] << ", "
		          << forecast_ci.upperSeries()[i] << "]\n";
	}
	std::cout.unsetf(std::ios::floatfield);
}

} // namespace

int main() {
	std::cout << "=== AutoARIMA Example Scenarios ===\n";
	std::cout << "Demonstrating automatic ARIMA model selection\n";
	
	// Scenario 1: AR(1) Process
	const auto ar_data = generateARProcess(150, 0.7);
	auto scenario1 = buildScenario("AR(1) Process", ar_data, 140);
	runScenario(scenario1);
	
	// Scenario 2: Trending Data
	const auto trend_data = generateTrendingSeries(120);
	auto scenario2 = buildScenario("Trending Series", trend_data, 100);
	runScenario(scenario2);
	
	// Scenario 3: Seasonal Data (monthly pattern)
	const auto seasonal_data = generateSeasonalSeries(156, 12);
	auto scenario3 = buildScenario("Seasonal Series (Monthly)", seasonal_data, 144, 12);
	runScenario(scenario3);
	
	// Scenario 4: AirPassengers Benchmark
	const auto air_data = airPassengersData();
	auto scenario4 = buildScenario("AirPassengers Benchmark", air_data, 36, 12);
	runScenario(scenario4);
	
	// Scenario 5: Comparison of Information Criteria
	std::cout << "\n=== Information Criteria Comparison ===\n";
	std::cout << "Comparing AIC, AICc, and BIC on same dataset\n\n";
	
	const auto comparison_data = generateTrendingSeries(100);
	auto ts_comparison = createTimeSeries(comparison_data);
	
	// AIC
	models::AutoARIMA model_aic(0);
	model_aic.setInformationCriterion(models::AutoARIMA::InformationCriterion::AIC);
	model_aic.fit(ts_comparison);
	const auto &comp_aic = model_aic.components();
	std::cout << "AIC  selected: ARIMA(" << comp_aic.p << "," << comp_aic.d << "," << comp_aic.q 
	          << ") with AIC=" << std::fixed << std::setprecision(2) << model_aic.metrics().aic << '\n';
	
	// AICc
	models::AutoARIMA model_aicc(0);
	model_aicc.setInformationCriterion(models::AutoARIMA::InformationCriterion::AICc);
	model_aicc.fit(ts_comparison);
	const auto &comp_aicc = model_aicc.components();
	std::cout << "AICc selected: ARIMA(" << comp_aicc.p << "," << comp_aicc.d << "," << comp_aicc.q 
	          << ") with AICc=" << std::fixed << std::setprecision(2) << model_aicc.metrics().aicc << '\n';
	
	// BIC
	models::AutoARIMA model_bic(0);
	model_bic.setInformationCriterion(models::AutoARIMA::InformationCriterion::BIC);
	model_bic.fit(ts_comparison);
	const auto &comp_bic = model_bic.components();
	std::cout << "BIC  selected: ARIMA(" << comp_bic.p << "," << comp_bic.d << "," << comp_bic.q 
	          << ") with BIC=" << std::fixed << std::setprecision(2) << model_bic.metrics().bic << '\n';
	
	std::cout << "\nNote: BIC typically selects simpler models (penalizes complexity more)\n";
	
	// Summary
	std::cout << "\n=== Summary ===\n\n";
	std::cout << "AutoARIMA automatically selects the best ARIMA/SARIMA model:\n\n";
	std::cout << "Key Features:\n";
	std::cout << "  • Automatic model selection via stepwise or exhaustive search\n";
	std::cout << "  • Differencing detection (d and D) using statistical tests\n";
	std::cout << "  • Full SARIMA support: ARIMA(p,d,q)(P,D,Q)[s]\n";
	std::cout << "  • Multiple information criteria: AIC, AICc, BIC\n";
	std::cout << "  • Confidence intervals and diagnostics\n\n";
	
	std::cout << "When to Use:\n";
	std::cout << "  • When you need automatic model selection\n";
	std::cout << "  • For seasonal data with unknown optimal parameters\n";
	std::cout << "  • When comparing multiple time series (same algorithm)\n";
	std::cout << "  • Production systems requiring robust forecasting\n\n";
	
	std::cout << "Performance Notes:\n";
	std::cout << "  • Stepwise search: Fast (~10-20 models evaluated)\n";
	std::cout << "  • Exhaustive search: Thorough but slower (~50+ models)\n";
	std::cout << "  • AICc recommended for small samples (n/k < 40)\n";
	std::cout << "  • BIC prefers simpler models (larger penalty term)\n\n";
	
	std::cout << "Comparison with Baselines:\n";
	std::cout << "  • Should beat SeasonalNaive by 30-50% on seasonal data\n";
	std::cout << "  • Should beat RandomWalkWithDrift on trending data\n";
	std::cout << "  • Typical improvement: 40-60% over simple baselines\n";
	
	return 0;
}

