#include "anofox-time/models/ses.hpp"
#include "anofox-time/models/ses_optimized.hpp"
#include "anofox-time/models/holt.hpp"
#include "anofox-time/models/ets.hpp"
#include "anofox-time/models/auto_ets.hpp"
#include "anofox-time/models/seasonal_es.hpp"
#include "anofox-time/models/seasonal_es_optimized.hpp"
#include "anofox-time/models/holt_winters.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/utils/metrics.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>

using namespace anofoxtime;

namespace {

// AirPassengers dataset (first 48 months)
std::vector<double> airPassengersData() {
	return {
		112., 118., 132., 129., 121., 135., 148., 148., 136., 119., 104., 118.,
		115., 126., 141., 135., 125., 149., 170., 170., 158., 133., 114., 140.,
		145., 150., 178., 163., 172., 178., 199., 199., 184., 162., 146., 166.,
		171., 180., 193., 181., 183., 218., 230., 242., 209., 191., 172., 194.
	};
}

std::vector<double> generateTrendingData(std::size_t n, double slope = 0.5) {
	std::vector<double> data(n);
	for (std::size_t i = 0; i < n; ++i) {
		data[i] = 50.0 + slope * static_cast<double>(i);
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
	std::cout << "\n=== " << title << " ===\n\n";
}

void printForecast(const std::string& method, const core::Forecast& forecast, int show_n = 5) {
	std::cout << "  " << std::setw(30) << std::left << method << ": ";
	const auto& values = forecast.primary();
	for (int i = 0; i < std::min(show_n, static_cast<int>(values.size())); ++i) {
		std::cout << std::fixed << std::setprecision(2) << values[i];
		if (i < show_n - 1 && i < static_cast<int>(values.size()) - 1) std::cout << ", ";
	}
	std::cout << "\n";
	std::cout.unsetf(std::ios::floatfield);
}

void printMetrics(const std::string& method, const std::vector<double>& actual, 
                  const std::vector<double>& forecast) {
	if (actual.size() != forecast.size() || actual.empty()) {
		return;
	}
	
	const double mae = utils::Metrics::mae(actual, forecast);
	const double rmse = utils::Metrics::rmse(actual, forecast);
	const auto smape_opt = utils::Metrics::smape(actual, forecast);
	
	std::cout << "  " << std::setw(30) << std::left << method << " | ";
	std::cout << "MAE: " << std::fixed << std::setprecision(2) << std::setw(7) << mae << " | ";
	std::cout << "RMSE: " << std::setw(7) << rmse << " | ";
	if (smape_opt && std::isfinite(*smape_opt)) {
		std::cout << "sMAPE: " << std::setw(6) << std::setprecision(2) << *smape_opt << "%";
	}
	std::cout << "\n";
	std::cout.unsetf(std::ios::floatfield);
}

} // namespace

int main() {
	std::cout << "=== Exponential Smoothing Methods Examples ===\n";
	std::cout << "Demonstrating SES, Holt's Linear Trend, and AutoETS\n";
	
	// ===================================================================
	// Scenario 1: Simple Trending Data (SES vs Holt)
	// ===================================================================
	printHeader("Scenario 1: Trending Data");
	
	const auto trending_data = generateTrendingData(50, 0.8);
	auto ts_trend = createTimeSeries(trending_data);
	
	std::cout << "Data: 50 points with linear trend (slope=0.8)\n";
	std::cout << "Forecast horizon: 10 points\n\n";
	
	// Simple Exponential Smoothing (no trend)
	auto ses = models::SimpleExponentialSmoothingBuilder()
		.withAlpha(0.3)
		.build();
	ses->fit(ts_trend);
	auto f_ses = ses->predict(10);
	printForecast("SES (α=0.3)", f_ses);
	
	// Holt's Linear Trend (with trend)
	auto holt = models::HoltLinearTrendBuilder()
		.withAlpha(0.8)
		.withBeta(0.2)
		.build();
	holt->fit(ts_trend);
	auto f_holt = holt->predict(10);
	printForecast("Holt's Linear Trend", f_holt);
	
	std::cout << "\n  Note: Holt captures the trend, SES gives flat forecast\n";
	
	// ===================================================================
	// Scenario 2: AirPassengers with Manual ETS
	// ===================================================================
	printHeader("Scenario 2: AirPassengers with Manual ETS");
	
	const auto air_data = airPassengersData();
	const std::size_t train_size = 36;
	std::vector<double> train_data(air_data.begin(), air_data.begin() + train_size);
	std::vector<double> test_data(air_data.begin() + train_size, air_data.end());
	
	auto ts_air = createTimeSeries(train_data);
	
	std::cout << "Dataset: AirPassengers\n";
	std::cout << "Training: 36 months (3 years)\n";
	std::cout << "Testing:  12 months (1 year)\n";
	std::cout << "Seasonal period: 12 (monthly)\n\n";
	
	std::cout << "Manual ETS Model Configurations:\n";
	std::cout << "  " << std::string(85, '-') << "\n";
	
	// ETS(A,N,A) - Additive error, No trend, Additive seasonality
	models::ETSConfig config_ana;
	config_ana.error = models::ETSErrorType::Additive;
	config_ana.trend = models::ETSTrendType::None;
	config_ana.season = models::ETSSeasonType::Additive;
	config_ana.season_length = 12;
	config_ana.alpha = 0.2;
	config_ana.gamma = 0.1;
	
	models::ETS ets_ana(config_ana);
	ets_ana.fit(ts_air);
	auto f_ana = ets_ana.predict(12);
	printMetrics("ETS(A,N,A)", test_data, f_ana.primary());
	
	// ETS(A,A,A) - Additive error, Additive trend, Additive seasonality
	models::ETSConfig config_aaa;
	config_aaa.error = models::ETSErrorType::Additive;
	config_aaa.trend = models::ETSTrendType::Additive;
	config_aaa.season = models::ETSSeasonType::Additive;
	config_aaa.season_length = 12;
	config_aaa.alpha = 0.2;
	config_aaa.beta = 0.1;
	config_aaa.gamma = 0.1;
	
	models::ETS ets_aaa(config_aaa);
	ets_aaa.fit(ts_air);
	auto f_aaa = ets_aaa.predict(12);
	printMetrics("ETS(A,A,A) - Holt-Winters", test_data, f_aaa.primary());
	
	// ETS(A,A,M) - Multiplicative seasonality
	models::ETSConfig config_aam;
	config_aam.error = models::ETSErrorType::Additive;
	config_aam.trend = models::ETSTrendType::Additive;
	config_aam.season = models::ETSSeasonType::Multiplicative;
	config_aam.season_length = 12;
	config_aam.alpha = 0.2;
	config_aam.beta = 0.1;
	config_aam.gamma = 0.1;
	
	models::ETS ets_aam(config_aam);
	ets_aam.fit(ts_air);
	auto f_aam = ets_aam.predict(12);
	printMetrics("ETS(A,A,M)", test_data, f_aam.primary());
	
	std::cout << "  " << std::string(85, '-') << "\n";
	
	// ===================================================================
	// Scenario 3: AutoETS Automatic Selection
	// ===================================================================
	printHeader("Scenario 3: AutoETS Automatic Model Selection");
	
	std::cout << "AutoETS automatically selects the best ETS model configuration\n";
	std::cout << "Dataset: AirPassengers (36 train → 12 test months)\n\n";
	
	models::AutoETS auto_ets(12, "ZZZ");  // Monthly seasonality, auto-select all components
	auto_ets.fit(ts_air);
	
	const auto& selected = auto_ets.components();
	std::cout << "  Selected ETS model: ";
	if (selected.error == models::AutoETSErrorType::Additive) std::cout << "A";
	else std::cout << "M";
	std::cout << ",";
	if (selected.trend == models::AutoETSTrendType::None) std::cout << "N";
	else if (selected.trend == models::AutoETSTrendType::Additive) std::cout << "A";
	else std::cout << "M";
	if (selected.damped) std::cout << "d";
	std::cout << ",";
	if (selected.season == models::AutoETSSeasonType::None) std::cout << "N";
	else if (selected.season == models::AutoETSSeasonType::Additive) std::cout << "A";
	else std::cout << "M";
	std::cout << "\n";
	
	const auto& params = auto_ets.parameters();
	std::cout << "  Model parameters:\n";
	std::cout << "    Alpha (level): " << std::fixed << std::setprecision(4) << params.alpha << "\n";
	if (selected.trend != models::AutoETSTrendType::None && std::isfinite(params.beta)) {
		std::cout << "    Beta (trend):  " << params.beta << "\n";
	}
	if (selected.season != models::AutoETSSeasonType::None && std::isfinite(params.gamma)) {
		std::cout << "    Gamma (season): " << params.gamma << "\n";
	}
	
	const auto& ets_metrics = auto_ets.metrics();
	std::cout << "  Model fit:\n";
	std::cout << "    AICc: " << std::fixed << std::setprecision(2) << ets_metrics.aicc << "\n";
	std::cout << "    AIC:  " << ets_metrics.aic << "\n";
	std::cout << "    BIC:  " << ets_metrics.bic << "\n\n";
	
	auto f_auto = auto_ets.predict(12);
	printMetrics("AutoETS", test_data, f_auto.primary());
	
	// ===================================================================
	// Scenario 4: Method Comparison on AirPassengers
	// ===================================================================
	printHeader("Scenario 4: All Methods Comparison");
	
	std::cout << "Comparing all exponential smoothing methods on AirPassengers\n\n";
	std::cout << "  Method                         | MAE     | RMSE    | sMAPE\n";
	std::cout << "  " << std::string(68, '-') << "\n";
	
	// SES
	auto ses_comp = models::SimpleExponentialSmoothingBuilder()
		.withAlpha(0.5)
		.build();
	ses_comp->fit(ts_air);
	auto f_ses_comp = ses_comp->predict(12);
	printMetrics("SES (α=0.5)", test_data, f_ses_comp.primary());
	
	// Holt
	auto holt_comp = models::HoltLinearTrendBuilder()
		.withAlpha(0.8)
		.withBeta(0.2)
		.build();
	holt_comp->fit(ts_air);
	auto f_holt_comp = holt_comp->predict(12);
	printMetrics("Holt's Linear Trend", test_data, f_holt_comp.primary());
	
	// Holt-Winters (ETS with seasonality)
	printMetrics("ETS(A,A,A) - Holt-Winters", test_data, f_aaa.primary());
	printMetrics("ETS(A,A,M)", test_data, f_aam.primary());
	
	// AutoETS
	printMetrics("AutoETS (optimal) ⭐", test_data, f_auto.primary());
	
	std::cout << "  " << std::string(68, '-') << "\n";
	std::cout << "  ⭐ AutoETS automatically selects the best configuration\n";
	
	// ===================================================================
	// Scenario 5: Optimized Methods
	// ===================================================================
	printHeader("Scenario 5: Optimized Methods");
	
	std::cout << "Automatic parameter optimization for exponential smoothing\n";
	std::cout << "Dataset: AirPassengers (36 train → 12 test)\n\n";
	
	// SES Optimized
	models::SESOptimized ses_opt;
	ses_opt.fit(ts_air);
	auto f_ses_opt = ses_opt.predict(12);
	std::cout << "  SESOptimized:\n";
	std::cout << "    Optimal alpha: " << std::fixed << std::setprecision(3) << ses_opt.optimalAlpha() << "\n";
	printMetrics("    ", test_data, f_ses_opt.primary());
	
	// Seasonal ES Optimized
	models::SeasonalESOptimized seas_es_opt(12);
	seas_es_opt.fit(ts_air);
	auto f_seas_opt = seas_es_opt.predict(12);
	std::cout << "\n  SeasonalESOptimized:\n";
	std::cout << "    Optimal alpha: " << std::fixed << std::setprecision(3) << seas_es_opt.optimalAlpha() << "\n";
	std::cout << "    Optimal gamma: " << std::fixed << std::setprecision(3) << seas_es_opt.optimalGamma() << "\n";
	printMetrics("    ", test_data, f_seas_opt.primary());
	
	std::cout << "\n  Note: Optimization finds parameters that minimize MSE\n";
	
	// ===================================================================
	// Scenario 6: Complete Ranking on AirPassengers
	// ===================================================================
	printHeader("Scenario 6: Complete Exponential Smoothing Ranking");
	
	std::cout << "All methods tested on AirPassengers (36 train → 12 test)\n";
	std::cout << "Using default parameters for fair comparison\n\n";
	
	struct MethodResult {
		std::string name;
		double mae;
		double rmse;
		double smape;
		std::string params;
	};
	
	std::vector<MethodResult> results;
	
	// AutoETS (already computed)
	{
		double mae = utils::Metrics::mae(test_data, f_auto.primary());
		double rmse = utils::Metrics::rmse(test_data, f_auto.primary());
		auto smape = utils::Metrics::smape(test_data, f_auto.primary());
		results.push_back({"AutoETS", mae, rmse, smape.value_or(0.0), "Auto"});
	}
	
	// SeasonalESOptimized (already computed)
	{
		double mae = utils::Metrics::mae(test_data, f_seas_opt.primary());
		double rmse = utils::Metrics::rmse(test_data, f_seas_opt.primary());
		auto smape = utils::Metrics::smape(test_data, f_seas_opt.primary());
		results.push_back({"SeasonalESOptimized", mae, rmse, smape.value_or(0.0), "Auto"});
	}
	
	// ETS(A,N,A) manual (already computed)
	{
		double mae = utils::Metrics::mae(test_data, f_ana.primary());
		double rmse = utils::Metrics::rmse(test_data, f_ana.primary());
		auto smape = utils::Metrics::smape(test_data, f_ana.primary());
		results.push_back({"ETS(A,N,A)", mae, rmse, smape.value_or(0.0), "α=0.2,γ=0.1"});
	}
	
	// SeasonalES with default params
	models::SeasonalExponentialSmoothing seas_es_def(12, 0.2, 0.1);
	seas_es_def.fit(ts_air);
	{
		auto f = seas_es_def.predict(12);
		double mae = utils::Metrics::mae(test_data, f.primary());
		double rmse = utils::Metrics::rmse(test_data, f.primary());
		auto smape = utils::Metrics::smape(test_data, f.primary());
		results.push_back({"SeasonalES", mae, rmse, smape.value_or(0.0), "α=0.2,γ=0.1"});
	}
	
	// HoltWinters Additive (already computed as ETS(A,A,A))
	{
		double mae = utils::Metrics::mae(test_data, f_aaa.primary());
		double rmse = utils::Metrics::rmse(test_data, f_aaa.primary());
		auto smape = utils::Metrics::smape(test_data, f_aaa.primary());
		results.push_back({"HoltWinters(Additive)", mae, rmse, smape.value_or(0.0), "α=0.2,β=0.1,γ=0.1"});
	}
	
	// SESOptimized (already computed)
	{
		double mae = utils::Metrics::mae(test_data, f_ses_opt.primary());
		double rmse = utils::Metrics::rmse(test_data, f_ses_opt.primary());
		auto smape = utils::Metrics::smape(test_data, f_ses_opt.primary());
		results.push_back({"SESOptimized", mae, rmse, smape.value_or(0.0), "Auto"});
	}
	
	// SES fixed (already computed)
	{
		double mae = utils::Metrics::mae(test_data, f_ses_comp.primary());
		double rmse = utils::Metrics::rmse(test_data, f_ses_comp.primary());
		auto smape = utils::Metrics::smape(test_data, f_ses_comp.primary());
		results.push_back({"SES(α=0.5)", mae, rmse, smape.value_or(0.0), "Fixed"});
	}
	
	// Holt fixed (already computed)
	{
		double mae = utils::Metrics::mae(test_data, f_holt_comp.primary());
		double rmse = utils::Metrics::rmse(test_data, f_holt_comp.primary());
		auto smape = utils::Metrics::smape(test_data, f_holt_comp.primary());
		results.push_back({"Holt(α=0.8,β=0.2)", mae, rmse, smape.value_or(0.0), "Fixed"});
	}
	
	// Sort by MAE (ascending)
	std::sort(results.begin(), results.end(), 
	          [](const MethodResult& a, const MethodResult& b) { return a.mae < b.mae; });
	
	// Print ranking table
	std::cout << "  COMPLETE RANKING (sorted by MAE):\n";
	std::cout << "  " << std::string(90, '=') << "\n";
	std::cout << "  Rank | Method                       | MAE     | RMSE    | sMAPE   | Parameters\n";
	std::cout << "  " << std::string(90, '-') << "\n";
	
	for (std::size_t i = 0; i < results.size(); ++i) {
		const auto& r = results[i];
		std::cout << "  " << std::setw(4) << (i + 1) << " | "
		          << std::setw(28) << std::left << r.name << " | "
		          << std::fixed << std::setprecision(2) << std::setw(7) << std::right << r.mae << " | "
		          << std::setw(7) << r.rmse << " | "
		          << std::setw(6) << r.smape << "% | "
		          << std::left << r.params << "\n";
	}
	std::cout << "  " << std::string(90, '=') << "\n";
	
	std::cout << "\n  Key Insights:\n";
	std::cout << "    • AutoETS automatically selects the best configuration\n";
	std::cout << "    • Seasonal methods significantly outperform non-seasonal\n";
	std::cout << "    • Optimization (Auto params) often improves over defaults\n";
	std::cout << "    • For AirPassengers: NO TREND + Seasonal performs best\n";
	std::cout << "    • Methods with trend (Holt, Holt-Winters) perform worse (data has no strong linear trend)\n";
	
	// ===================================================================
	// Summary
	// ===================================================================
	std::cout << "\n=== Summary ===\n\n";
	std::cout << "Exponential Smoothing Family:\n\n";
	
	std::cout << "Methods:\n";
	std::cout << "  • SES: Simple Exponential Smoothing (level only)\n";
	std::cout << "  • Holt: Holt's Linear Trend (level + trend)\n";
	std::cout << "  • ETS: Error-Trend-Season framework (15 models)\n";
	std::cout << "  • AutoETS: Automatic ETS model selection\n\n";
	
	std::cout << "ETS Framework:\n";
	std::cout << "  Error:  A (Additive) or M (Multiplicative)\n";
	std::cout << "  Trend:  N (None), A (Additive), M (Multiplicative), Ad (Damped Additive)\n";
	std::cout << "  Season: N (None), A (Additive), M (Multiplicative)\n\n";
	
	std::cout << "Common Models:\n";
	std::cout << "  • ETS(A,N,N) = SES\n";
	std::cout << "  • ETS(A,A,N) = Holt's Linear Trend\n";
	std::cout << "  • ETS(A,A,A) = Additive Holt-Winters\n";
	std::cout << "  • ETS(A,A,M) = Multiplicative Holt-Winters\n\n";
	
	std::cout << "When to Use:\n";
	std::cout << "  • SES: Stationary data, no trend or seasonality\n";
	std::cout << "  • Holt: Trending data without seasonality\n";
	std::cout << "  • Holt-Winters: Seasonal data with trend\n";
	std::cout << "  • AutoETS: When you want automatic model selection\n\n";
	
	std::cout << "Advantages:\n";
	std::cout << "  • Fast fitting and forecasting\n";
	std::cout << "  • Smooth forecasts (weighted recent observations)\n";
	std::cout << "  • Well-suited for short-term forecasting\n";
	std::cout << "  • Interpretable parameters (α, β, γ)\n";
	std::cout << "  • State space formulation allows confidence intervals\n\n";
	
	std::cout << "Comparison with Other Methods:\n";
	std::cout << "  vs Baselines:\n";
	std::cout << "    • Usually 20-40% better than SeasonalNaive\n";
	std::cout << "    • Smoother forecasts, less jumpy\n";
	std::cout << "  vs ARIMA:\n";
	std::cout << "    • Faster to fit\n";
	std::cout << "    • Better for data with clear trend/seasonality\n";
	std::cout << "    • ARIMA better for complex autocorrelation patterns\n";
	std::cout << "  vs Theta:\n";
	std::cout << "    • ETS more flexible (15 model types)\n";
	std::cout << "    • Theta simpler, faster\n";
	std::cout << "    • Performance similar on many datasets\n";
	
	return 0;
}

