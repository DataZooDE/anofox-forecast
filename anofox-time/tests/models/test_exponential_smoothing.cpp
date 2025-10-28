#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/models/ses_optimized.hpp"
#include "anofox-time/models/seasonal_es.hpp"
#include "anofox-time/models/seasonal_es_optimized.hpp"
#include "anofox-time/models/holt_winters.hpp"
#include "common/time_series_helpers.hpp"

#include <cmath>
#include <vector>

using anofoxtime::models::SESOptimized;
using anofoxtime::models::SeasonalExponentialSmoothing;
using anofoxtime::models::SeasonalESOptimized;
using anofoxtime::models::HoltWinters;

namespace {

std::vector<double> generateTrendingData(std::size_t n, double slope = 0.5) {
	std::vector<double> data(n);
	for (std::size_t i = 0; i < n; ++i) {
		data[i] = 100.0 + slope * static_cast<double>(i);
	}
	return data;
}

std::vector<double> generateSeasonalData(std::size_t cycles, int period = 12) {
	std::vector<double> data;
	for (std::size_t c = 0; c < cycles; ++c) {
		for (int t = 0; t < period; ++t) {
			const double seasonal = 10.0 * std::sin(2.0 * M_PI * static_cast<double>(t) / static_cast<double>(period));
			const double trend = 100.0 + 0.5 * static_cast<double>(c * period + t);
			data.push_back(trend + seasonal);
		}
	}
	return data;
}

} // namespace

// ==========================
// SESOptimized Tests
// ==========================

TEST_CASE("SESOptimized finds optimal alpha", "[models][exp_smooth][ses_opt]") {
	const auto data = generateTrendingData(40);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SESOptimized model;
	model.fit(ts);
	
	REQUIRE(model.optimalAlpha() >= 0.05);
	REQUIRE(model.optimalAlpha() <= 0.95);
	REQUIRE(std::isfinite(model.optimalMSE()));
	REQUIRE(model.optimalMSE() > 0.0);
}

TEST_CASE("SESOptimized produces forecasts", "[models][exp_smooth][ses_opt]") {
	const auto data = generateTrendingData(30);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SESOptimized model;
	model.fit(ts);
	
	auto forecast = model.predict(5);
	REQUIRE(forecast.primary().size() == 5);
	
	// SES gives flat forecasts
	REQUIRE(forecast.primary()[0] == Catch::Approx(forecast.primary()[4]).margin(0.01));
}

TEST_CASE("SESOptimized handles short series", "[models][exp_smooth][ses_opt]") {
	const std::vector<double> short_data = {10.0, 12.0, 15.0};
	auto ts = tests::helpers::makeUnivariateSeries(short_data);
	
	SESOptimized model;
	REQUIRE_NOTHROW(model.fit(ts));
	REQUIRE_NOTHROW(model.predict(3));
}

TEST_CASE("SESOptimized getName", "[models][exp_smooth][ses_opt]") {
	SESOptimized model;
	REQUIRE(model.getName() == "SESOptimized");
}

TEST_CASE("SESOptimized empty data error", "[models][exp_smooth][ses_opt][error]") {
	std::vector<double> empty;
	auto ts = tests::helpers::makeUnivariateSeries(empty);
	
	SESOptimized model;
	REQUIRE_THROWS_AS(model.fit(ts), std::invalid_argument);
}

// ==========================
// SeasonalES Tests
// ==========================

TEST_CASE("SeasonalES constructor validates parameters", "[models][exp_smooth][seasonal_es]") {
	REQUIRE_NOTHROW(SeasonalExponentialSmoothing(12, 0.2, 0.1));
	REQUIRE_THROWS_AS(SeasonalExponentialSmoothing(1, 0.2, 0.1), std::invalid_argument);
	REQUIRE_THROWS_AS(SeasonalExponentialSmoothing(12, 1.5, 0.1), std::invalid_argument);
	REQUIRE_THROWS_AS(SeasonalExponentialSmoothing(12, 0.2, -0.1), std::invalid_argument);
}

TEST_CASE("SeasonalES basic forecasting", "[models][exp_smooth][seasonal_es]") {
	const auto data = generateSeasonalData(5, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalExponentialSmoothing model(12, 0.2, 0.1);
	model.fit(ts);
	
	REQUIRE(model.seasonalPeriod() == 12);
	REQUIRE(model.alpha() == Catch::Approx(0.2));
	REQUIRE(model.gamma() == Catch::Approx(0.1));
	
	auto forecast = model.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("SeasonalES quarterly seasonality", "[models][exp_smooth][seasonal_es]") {
	const auto data = generateSeasonalData(8, 4);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalExponentialSmoothing model(4, 0.3, 0.2);
	model.fit(ts);
	
	auto forecast = model.predict(8);
	REQUIRE(forecast.primary().size() == 8);
}

TEST_CASE("SeasonalES fitted values and residuals", "[models][exp_smooth][seasonal_es]") {
	const auto data = generateSeasonalData(6, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalExponentialSmoothing model(12, 0.2, 0.1);
	model.fit(ts);
	
	const auto& fitted = model.fittedValues();
	const auto& residuals = model.residuals();
	
	REQUIRE(fitted.size() == data.size());
	REQUIRE(residuals.size() == data.size());
}

TEST_CASE("SeasonalES confidence intervals", "[models][exp_smooth][seasonal_es]") {
	const auto data = generateSeasonalData(10, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalExponentialSmoothing model(12, 0.2, 0.1);
	model.fit(ts);
	
	auto forecast = model.predictWithConfidence(12, 0.95);
	REQUIRE(forecast.lowerSeries().size() == 12);
	REQUIRE(forecast.upperSeries().size() == 12);
}

TEST_CASE("SeasonalES requires sufficient data", "[models][exp_smooth][seasonal_es][error]") {
	const std::vector<double> short_data = {10.0, 12.0, 15.0};
	auto ts = tests::helpers::makeUnivariateSeries(short_data);
	
	SeasonalExponentialSmoothing model(12, 0.2, 0.1);
	REQUIRE_THROWS_AS(model.fit(ts), std::invalid_argument);
}

TEST_CASE("SeasonalES parameter variations", "[models][exp_smooth][seasonal_es]") {
	const auto data = generateSeasonalData(8, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// Low smoothing
	SeasonalExponentialSmoothing model_low(12, 0.1, 0.05);
	REQUIRE_NOTHROW(model_low.fit(ts));
	
	// High smoothing
	SeasonalExponentialSmoothing model_high(12, 0.9, 0.8);
	REQUIRE_NOTHROW(model_high.fit(ts));
}

TEST_CASE("SeasonalES weekly data", "[models][exp_smooth][seasonal_es]") {
	const auto data = generateSeasonalData(10, 7);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalExponentialSmoothing model(7, 0.2, 0.1);
	model.fit(ts);
	
	auto forecast = model.predict(14);
	REQUIRE(forecast.primary().size() == 14);
}

// ==========================
// SeasonalESOptimized Tests
// ==========================

TEST_CASE("SeasonalESOptimized finds optimal parameters", "[models][exp_smooth][seasonal_es_opt]") {
	const auto data = generateSeasonalData(10, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalESOptimized model(12);
	model.fit(ts);
	
	REQUIRE(model.optimalAlpha() >= 0.05);
	REQUIRE(model.optimalAlpha() <= 0.95);
	REQUIRE(model.optimalGamma() >= 0.05);
	REQUIRE(model.optimalGamma() <= 0.95);
	REQUIRE(std::isfinite(model.optimalMSE()));
}

TEST_CASE("SeasonalESOptimized produces forecasts", "[models][exp_smooth][seasonal_es_opt]") {
	const auto data = generateSeasonalData(8, 4);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalESOptimized model(4);
	model.fit(ts);
	
	auto forecast = model.predict(8);
	REQUIRE(forecast.primary().size() == 8);
}

TEST_CASE("SeasonalESOptimized monthly data", "[models][exp_smooth][seasonal_es_opt]") {
	const auto data = generateSeasonalData(12, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalESOptimized model(12);
	model.fit(ts);
	
	auto forecast = model.predict(12);
	REQUIRE(forecast.primary().size() == 12);
	
	// Check fitted values
	const auto& fitted = model.fittedValues();
	REQUIRE(fitted.size() == data.size());
}

TEST_CASE("SeasonalESOptimized confidence intervals", "[models][exp_smooth][seasonal_es_opt]") {
	const auto data = generateSeasonalData(10, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalESOptimized model(12);
	model.fit(ts);
	
	auto forecast = model.predictWithConfidence(12, 0.95);
	REQUIRE(forecast.lowerSeries().size() == 12);
	REQUIRE(forecast.upperSeries().size() == 12);
}

TEST_CASE("SeasonalESOptimized invalid period", "[models][exp_smooth][seasonal_es_opt][error]") {
	REQUIRE_THROWS_AS(SeasonalESOptimized(1), std::invalid_argument);
	REQUIRE_THROWS_AS(SeasonalESOptimized(0), std::invalid_argument);
}

TEST_CASE("SeasonalESOptimized getName", "[models][exp_smooth][seasonal_es_opt]") {
	SeasonalESOptimized model(12);
	REQUIRE(model.getName() == "SeasonalESOptimized");
}

// ==========================
// HoltWinters Tests
// ==========================

TEST_CASE("HoltWinters additive seasonality", "[models][exp_smooth][holt_winters]") {
	const auto data = generateSeasonalData(8, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	HoltWinters model(12, HoltWinters::SeasonType::Additive, 0.2, 0.1, 0.1);
	model.fit(ts);
	
	REQUIRE(model.seasonalPeriod() == 12);
	REQUIRE(model.seasonType() == HoltWinters::SeasonType::Additive);
	
	auto forecast = model.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("HoltWinters multiplicative seasonality", "[models][exp_smooth][holt_winters]") {
	const auto data = generateSeasonalData(8, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	HoltWinters model(12, HoltWinters::SeasonType::Multiplicative, 0.2, 0.1, 0.1);
	model.fit(ts);
	
	REQUIRE(model.seasonType() == HoltWinters::SeasonType::Multiplicative);
	
	auto forecast = model.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("HoltWinters quarterly data", "[models][exp_smooth][holt_winters]") {
	const auto data = generateSeasonalData(10, 4);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	HoltWinters model(4, HoltWinters::SeasonType::Additive);
	model.fit(ts);
	
	auto forecast = model.predict(8);
	REQUIRE(forecast.primary().size() == 8);
}

TEST_CASE("HoltWinters fitted values and residuals", "[models][exp_smooth][holt_winters]") {
	const auto data = generateSeasonalData(8, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	HoltWinters model(12, HoltWinters::SeasonType::Additive, 0.2, 0.1, 0.1);
	model.fit(ts);
	
	const auto& fitted = model.fittedValues();
	const auto& residuals = model.residuals();
	
	REQUIRE(!fitted.empty());
	REQUIRE(!residuals.empty());
}

TEST_CASE("HoltWinters default parameters", "[models][exp_smooth][holt_winters]") {
	const auto data = generateSeasonalData(8, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// Use default α=0.2, β=0.1, γ=0.1
	HoltWinters model(12, HoltWinters::SeasonType::Additive);
	REQUIRE_NOTHROW(model.fit(ts));
}

TEST_CASE("HoltWinters invalid period", "[models][exp_smooth][holt_winters][error]") {
	REQUIRE_THROWS_AS(
		HoltWinters(1, HoltWinters::SeasonType::Additive),
		std::invalid_argument
	);
}

TEST_CASE("HoltWinters getName", "[models][exp_smooth][holt_winters]") {
	HoltWinters model(12, HoltWinters::SeasonType::Additive);
	REQUIRE(model.getName() == "HoltWinters");
}

TEST_CASE("HoltWinters weekly seasonality", "[models][exp_smooth][holt_winters]") {
	const auto data = generateSeasonalData(15, 7);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	HoltWinters model(7, HoltWinters::SeasonType::Multiplicative, 0.3, 0.2, 0.2);
	model.fit(ts);
	
	auto forecast = model.predict(14);
	REQUIRE(forecast.primary().size() == 14);
}

// ==========================
// Integration Tests
// ==========================

TEST_CASE("All new ES methods on same data", "[models][exp_smooth][integration]") {
	const auto data = generateSeasonalData(10, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SESOptimized ses_opt;
	SeasonalESOptimized seas_opt(12);
	SeasonalExponentialSmoothing seas_manual(12, 0.2, 0.1);
	HoltWinters hw_add(12, HoltWinters::SeasonType::Additive);
	HoltWinters hw_mult(12, HoltWinters::SeasonType::Multiplicative);
	
	REQUIRE_NOTHROW(ses_opt.fit(ts));
	REQUIRE_NOTHROW(seas_opt.fit(ts));
	REQUIRE_NOTHROW(seas_manual.fit(ts));
	REQUIRE_NOTHROW(hw_add.fit(ts));
	REQUIRE_NOTHROW(hw_mult.fit(ts));
	
	const int horizon = 12;
	auto f1 = ses_opt.predict(horizon);
	auto f2 = seas_opt.predict(horizon);
	auto f3 = seas_manual.predict(horizon);
	auto f4 = hw_add.predict(horizon);
	auto f5 = hw_mult.predict(horizon);
	
	REQUIRE(f1.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(f2.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(f3.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(f4.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(f5.primary().size() == static_cast<std::size_t>(horizon));
}

TEST_CASE("ES methods getName returns correct identifiers", "[models][exp_smooth][metadata]") {
	SESOptimized ses_opt;
	SeasonalExponentialSmoothing seas_es(12, 0.2, 0.1);
	SeasonalESOptimized seas_opt(12);
	HoltWinters hw(12, HoltWinters::SeasonType::Additive);
	
	REQUIRE(ses_opt.getName() == "SESOptimized");
	REQUIRE(seas_es.getName() == "SeasonalExponentialSmoothing");
	REQUIRE(seas_opt.getName() == "SeasonalESOptimized");
	REQUIRE(hw.getName() == "HoltWinters");
}

TEST_CASE("SeasonalES vs HoltWinters additive comparison", "[models][exp_smooth][comparison]") {
	const auto data = generateSeasonalData(8, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SeasonalES has no trend, HoltWinters has trend
	// Both should forecast, but HoltWinters should capture trend better
	SeasonalExponentialSmoothing seas(12, 0.2, 0.1);
	HoltWinters hw(12, HoltWinters::SeasonType::Additive, 0.2, 0.1, 0.1);
	
	seas.fit(ts);
	hw.fit(ts);
	
	auto f_seas = seas.predict(12);
	auto f_hw = hw.predict(12);
	
	REQUIRE(f_seas.primary().size() == 12);
	REQUIRE(f_hw.primary().size() == 12);
	
	// Both should produce valid forecasts
	for (const double val : f_seas.primary()) {
		REQUIRE(std::isfinite(val));
	}
	for (const double val : f_hw.primary()) {
		REQUIRE(std::isfinite(val));
	}
}

