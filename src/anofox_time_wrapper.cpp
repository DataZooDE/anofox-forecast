// Define this BEFORE including any anofox-time headers
#ifndef ANOFOX_NO_LOGGING
#define ANOFOX_NO_LOGGING
#endif

// This file isolates all anofox-time includes to prevent namespace conflicts
#include "anofox_time_wrapper.hpp"

// Include anofox-time headers
#include "anofox-time/models/sma.hpp"
#include "anofox-time/models/naive.hpp"
#include "anofox-time/models/seasonal_naive.hpp"
#include "anofox-time/models/ses.hpp"
#include "anofox-time/models/ses_optimized.hpp"
#include "anofox-time/models/theta.hpp"
#include "anofox-time/models/optimized_theta.hpp"
#include "anofox-time/models/dynamic_theta.hpp"
#include "anofox-time/models/dynamic_optimized_theta.hpp"
#include "anofox-time/models/auto_theta.hpp"
#include "anofox-time/models/holt.hpp"
#include "anofox-time/models/holt_winters.hpp"
#include "anofox-time/models/seasonal_es.hpp"
#include "anofox-time/models/seasonal_es_optimized.hpp"
#include "anofox-time/models/seasonal_window_average.hpp"
#include "anofox-time/models/arima.hpp"
#include "anofox-time/models/auto_arima.hpp"
#include "anofox-time/models/ets.hpp"
#include "anofox-time/models/auto_ets.hpp"
#include "anofox-time/models/mfles.hpp"
#include "anofox-time/models/auto_mfles.hpp"
#include "anofox-time/models/mstl_forecaster.hpp"
#include "anofox-time/models/auto_mstl.hpp"
#include "anofox-time/models/tbats.hpp"
#include "anofox-time/models/auto_tbats.hpp"
#include "anofox-time/models/croston_classic.hpp"
#include "anofox-time/models/croston_optimized.hpp"
#include "anofox-time/models/croston_sba.hpp"
#include "anofox-time/models/adida.hpp"
#include "anofox-time/models/imapa.hpp"
#include "anofox-time/models/tsb.hpp"
#include "anofox-time/models/random_walk_drift.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"

// Now include std headers
#include <iostream>

// Explicit namespace to avoid pollution
namespace duckdb {

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateNaive() {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateNaive" << std::endl;
	return std::make_unique<::anofoxtime::models::Naive>();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateSeasonalNaive(int period) {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateSeasonalNaive with period: " << period << std::endl;
	return std::make_unique<::anofoxtime::models::SeasonalNaive>(period);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateSMA(int window) {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateSMA with window: " << window << std::endl;
	return ::anofoxtime::models::SimpleMovingAverageBuilder().withWindow(window).build();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateSES(double alpha) {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateSES with alpha: " << alpha << std::endl;
	return ::anofoxtime::models::SimpleExponentialSmoothingBuilder().withAlpha(alpha).build();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateTheta(int seasonal_period,
                                                                                  double theta_param) {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateTheta with period: " << seasonal_period
	//           << ", theta: " << theta_param << std::endl;
	return std::make_unique<::anofoxtime::models::Theta>(seasonal_period, theta_param);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateHolt(double alpha, double beta) {
	// statsforecast's Holt is actually AutoETS(model="AAN")
	// However, we pin alpha/beta parameters for better performance
	// This avoids unnecessary optimization while maintaining statsforecast alignment
	auto model = std::make_unique<::anofoxtime::models::AutoETS>(1, "AAN");
	model->setPinnedAlpha(alpha);
	model->setPinnedBeta(beta);
	return model;
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateHoltWinters(int seasonal_period,
                                                                                        bool multiplicative,
                                                                                        double alpha, double beta,
                                                                                        double gamma) {
	// statsforecast's HoltWinters is AutoETS, and for AirPassengers it selects damped models
	// AAA actually selects AAdA (damped additive trend)
	// MAM actually selects MAdM (damped multiplicative trend)
	// Using the damped specs ensures better alignment with statsforecast's selection
	std::string model = multiplicative ? "MAdM" : "AAdA";
	return std::make_unique<::anofoxtime::models::AutoETS>(seasonal_period, model);
}

#ifdef HAVE_EIGEN3
std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateAutoARIMA(int seasonal_period) {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateAutoARIMA with period: " << seasonal_period << std::endl;
	return std::make_unique<::anofoxtime::models::AutoARIMA>(seasonal_period);
}
#endif

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateETS(int error_type, int trend_type,
                                                                                int season_type, int season_length,
                                                                                double alpha, double beta, double gamma,
                                                                                double phi) {

	// Map integer types to enums
	::anofoxtime::models::ETSErrorType error;
	switch (error_type) {
	case 0:
		error = ::anofoxtime::models::ETSErrorType::Additive;
		break;
	case 1:
		error = ::anofoxtime::models::ETSErrorType::Multiplicative;
		break;
	default:
		error = ::anofoxtime::models::ETSErrorType::Additive;
		break;
	}

	::anofoxtime::models::ETSTrendType trend;
	switch (trend_type) {
	case 0:
		trend = ::anofoxtime::models::ETSTrendType::None;
		break;
	case 1:
		trend = ::anofoxtime::models::ETSTrendType::Additive;
		break;
	case 2:
		trend = ::anofoxtime::models::ETSTrendType::Multiplicative;
		break;
	case 3:
		trend = ::anofoxtime::models::ETSTrendType::DampedAdditive;
		break;
	case 4:
		trend = ::anofoxtime::models::ETSTrendType::DampedMultiplicative;
		break;
	default:
		trend = ::anofoxtime::models::ETSTrendType::None;
		break;
	}

	::anofoxtime::models::ETSSeasonType season;
	switch (season_type) {
	case 0:
		season = ::anofoxtime::models::ETSSeasonType::None;
		break;
	case 1:
		season = ::anofoxtime::models::ETSSeasonType::Additive;
		break;
	case 2:
		season = ::anofoxtime::models::ETSSeasonType::Multiplicative;
		break;
	default:
		season = ::anofoxtime::models::ETSSeasonType::None;
		break;
	}

	// Build ETS config
	auto builder = ::anofoxtime::models::ETSBuilder()
	                   .withError(error)
	                   .withTrend(trend)
	                   .withSeason(season, season_length)
	                   .withAlpha(alpha);

	// Only set optional parameters if trend/season is present
	if (trend_type != 0) {
		builder.withBeta(beta);
	}
	if (season_type != 0) {
		builder.withGamma(gamma);
	}
	if (trend_type == 3 || trend_type == 4) { // Damped trends
		builder.withPhi(phi);
	}

	return builder.build();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateAutoETS(int season_length,
                                                                                    const std::string &model) {
	// model spec format: error-trend-season (e.g., "AAA", "MAN", "ZZZ")
	// Default is "ZZZ" (auto-select all components)
	return std::make_unique<::anofoxtime::models::AutoETS>(season_length, model);
}

std::unique_ptr<::anofoxtime::models::IForecaster>
AnofoxTimeWrapper::CreateMFLES(const std::vector<int> &seasonal_periods, int n_iterations, double lr_trend,
                               double lr_season, double lr_level, bool progressive_trend, bool sequential_seasonality) {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateMFLES with " << seasonal_periods.size() << " periods" <<
	// std::endl;
	::anofoxtime::models::MFLES::Params params;
	params.seasonal_periods = seasonal_periods;
	params.max_rounds = n_iterations;
	params.lr_trend = lr_trend;
	params.lr_season = lr_season;
	params.lr_rs = lr_level;
	params.progressive_trend = progressive_trend;
	params.sequential_seasonality = sequential_seasonality;
	return std::make_unique<::anofoxtime::models::MFLES>(params);
}

std::unique_ptr<::anofoxtime::models::IForecaster>
AnofoxTimeWrapper::CreateAutoMFLES(const std::vector<int> &seasonal_periods, int max_rounds, double lr_trend,
                                   double lr_season, double lr_rs, int cv_horizon, int cv_n_windows,
                                   ::anofoxtime::utils::CVMetric cv_metric) {
	::anofoxtime::models::AutoMFLES::Config config;
	config.seasonal_periods = seasonal_periods;
	config.max_rounds = max_rounds;
	config.lr_trend = lr_trend;
	config.lr_season = lr_season;
	config.lr_rs = lr_rs;
	config.cv_horizon = cv_horizon;
	config.cv_n_windows = cv_n_windows;
	config.cv_metric = cv_metric;
	return std::make_unique<::anofoxtime::models::AutoMFLES>(config);
}

std::unique_ptr<::anofoxtime::models::IForecaster>
AnofoxTimeWrapper::CreateMSTL(const std::vector<int> &seasonal_periods, int trend_method, int seasonal_method,
                               int deseasonalized_method) {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateMSTL" << std::endl;

	// Map integer to enum
	::anofoxtime::models::MSTLForecaster::TrendMethod trend;
	switch (trend_method) {
	case 0:
		trend = ::anofoxtime::models::MSTLForecaster::TrendMethod::Linear;
		break;
	case 1:
		trend = ::anofoxtime::models::MSTLForecaster::TrendMethod::SES;
		break;
	case 2:
		trend = ::anofoxtime::models::MSTLForecaster::TrendMethod::Holt;
		break;
	case 3:
		trend = ::anofoxtime::models::MSTLForecaster::TrendMethod::None;
		break;
	case 4:
		trend = ::anofoxtime::models::MSTLForecaster::TrendMethod::AutoETSTrendAdditive;
		break;
	case 5:
		trend = ::anofoxtime::models::MSTLForecaster::TrendMethod::AutoETSTrendMultiplicative;
		break;
	default:
		trend = ::anofoxtime::models::MSTLForecaster::TrendMethod::Linear;
		break;
	}

	::anofoxtime::models::MSTLForecaster::SeasonalMethod season;
	switch (seasonal_method) {
	case 0:
		season = ::anofoxtime::models::MSTLForecaster::SeasonalMethod::Cyclic;
		break;
	case 1:
		season = ::anofoxtime::models::MSTLForecaster::SeasonalMethod::AutoETSAdditive;
		break;
	case 2:
		season = ::anofoxtime::models::MSTLForecaster::SeasonalMethod::AutoETSMultiplicative;
		break;
	default:
		season = ::anofoxtime::models::MSTLForecaster::SeasonalMethod::Cyclic;
		break;
	}

	::anofoxtime::models::MSTLForecaster::DeseasonalizedForecastMethod deseas;
	switch (deseasonalized_method) {
	case 0:
		deseas = ::anofoxtime::models::MSTLForecaster::DeseasonalizedForecastMethod::ExponentialSmoothing;
		break;
	case 1:
		deseas = ::anofoxtime::models::MSTLForecaster::DeseasonalizedForecastMethod::Linear;
		break;
	case 2:
		deseas = ::anofoxtime::models::MSTLForecaster::DeseasonalizedForecastMethod::AutoETS;
		break;
	default:
		deseas = ::anofoxtime::models::MSTLForecaster::DeseasonalizedForecastMethod::ExponentialSmoothing;
		break;
	}

	return std::make_unique<::anofoxtime::models::MSTLForecaster>(seasonal_periods, trend, season, deseas);
}

std::unique_ptr<::anofoxtime::models::IForecaster>
AnofoxTimeWrapper::CreateAutoMSTL(const std::vector<int> &seasonal_periods) {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateAutoMSTL with " << seasonal_periods.size() << " periods" <<
	// std::endl;
	return std::make_unique<::anofoxtime::models::AutoMSTL>(seasonal_periods);
}

// Additional basic models
std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateRandomWalkWithDrift() {
	return std::make_unique<::anofoxtime::models::RandomWalkWithDrift>();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateSESOptimized() {
	return std::make_unique<::anofoxtime::models::SESOptimized>();
}

#ifdef HAVE_EIGEN3
// ARIMA (manual configuration)
std::unique_ptr<::anofoxtime::models::IForecaster>
AnofoxTimeWrapper::CreateARIMA(int p, int d, int q, int P, int D, int Q, int s, bool include_intercept) {
	return ::anofoxtime::models::ARIMABuilder()
	    .withAR(p)
	    .withDifferencing(d)
	    .withMA(q)
	    .withSeasonalAR(P)
	    .withSeasonalDifferencing(D)
	    .withSeasonalMA(Q)
	    .withSeasonalPeriod(s)
	    .withIntercept(include_intercept)
	    .build();
}
#endif

// TBATS
std::unique_ptr<::anofoxtime::models::IForecaster>
AnofoxTimeWrapper::CreateTBATS(const std::vector<int> &seasonal_periods, bool use_box_cox, double box_cox_lambda,
                               bool use_trend, bool use_damped_trend, double damping_param, int ar_order,
                               int ma_order) {

	::anofoxtime::models::TBATS::Config config;
	config.seasonal_periods = seasonal_periods;
	config.use_box_cox = use_box_cox;
	config.box_cox_lambda = box_cox_lambda;
	config.use_trend = use_trend;
	config.use_damped_trend = use_damped_trend;
	config.damping_param = damping_param;
	config.ar_order = ar_order;
	config.ma_order = ma_order;

	return std::make_unique<::anofoxtime::models::TBATS>(config);
}

std::unique_ptr<::anofoxtime::models::IForecaster>
AnofoxTimeWrapper::CreateAutoTBATS(const std::vector<int> &seasonal_periods) {
	return std::make_unique<::anofoxtime::models::AutoTBATS>(seasonal_periods);
}

// Theta variants
std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateOptimizedTheta(int seasonal_period) {
	return std::make_unique<::anofoxtime::models::OptimizedTheta>(seasonal_period);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateDynamicTheta(int seasonal_period,
                                                                                         double theta_param) {
	// DynamicTheta only takes seasonal_period, theta param is ignored (it's optimized internally)
	return std::make_unique<::anofoxtime::models::DynamicTheta>(seasonal_period);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateDynamicOptimizedTheta(int seasonal_period) {
	return std::make_unique<::anofoxtime::models::DynamicOptimizedTheta>(seasonal_period);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateAutoTheta(
	int seasonal_period,
	const std::string& decomposition_type,
	const std::optional<std::string>& specific_model,
	int nmse) {
	
	// Convert decomposition type string to enum
	::anofoxtime::models::AutoTheta::DecompositionType decomp_type;
	if (decomposition_type == "additive") {
		decomp_type = ::anofoxtime::models::AutoTheta::DecompositionType::Additive;
	} else if (decomposition_type == "multiplicative") {
		decomp_type = ::anofoxtime::models::AutoTheta::DecompositionType::Multiplicative;
	} else {
		decomp_type = ::anofoxtime::models::AutoTheta::DecompositionType::Auto;
	}
	
	return std::make_unique<::anofoxtime::models::AutoTheta>(
		seasonal_period, decomp_type, specific_model, nmse
	);
}

// Seasonal exponential smoothing
std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateSeasonalES(int seasonal_period,
                                                                                       double alpha, double gamma) {
	// Direct constructor, no builder
	return std::make_unique<::anofoxtime::models::SeasonalExponentialSmoothing>(seasonal_period, alpha, gamma);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateSeasonalESOptimized(int seasonal_period) {
	return std::make_unique<::anofoxtime::models::SeasonalESOptimized>(seasonal_period);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateSeasonalWindowAverage(int seasonal_period,
                                                                                                  int window) {
	return std::make_unique<::anofoxtime::models::SeasonalWindowAverage>(seasonal_period, window);
}

// Intermittent demand models
std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateCrostonClassic() {
	return std::make_unique<::anofoxtime::models::CrostonClassic>();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateCrostonOptimized() {
	return std::make_unique<::anofoxtime::models::CrostonOptimized>();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateCrostonSBA() {
	return std::make_unique<::anofoxtime::models::CrostonSBA>();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateADIDA() {
	return std::make_unique<::anofoxtime::models::ADIDA>();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateIMAPA() {
	return std::make_unique<::anofoxtime::models::IMAPA>();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateTSB(double alpha_d, double alpha_p) {
	return std::make_unique<::anofoxtime::models::TSB>(alpha_d, alpha_p);
}

std::unique_ptr<::anofoxtime::core::TimeSeries>
AnofoxTimeWrapper::BuildTimeSeries(const std::vector<std::chrono::system_clock::time_point> &timestamps,
                                   const std::vector<double> &values) {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::BuildTimeSeries with " << timestamps.size() << " points" << std::endl;

	std::vector<::anofoxtime::core::TimeSeries::TimePoint> ts_timestamps;
	ts_timestamps.reserve(timestamps.size());
	for (const auto &tp : timestamps) {
		ts_timestamps.push_back(tp);
	}

	return std::make_unique<::anofoxtime::core::TimeSeries>(std::move(ts_timestamps), values);
}

void AnofoxTimeWrapper::FitModel(::anofoxtime::models::IForecaster *model, const ::anofoxtime::core::TimeSeries &ts) {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::FitModel" << std::endl;
	model->fit(ts);
}

std::unique_ptr<::anofoxtime::core::Forecast> AnofoxTimeWrapper::Predict(::anofoxtime::models::IForecaster *model,
                                                                         int horizon) {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::Predict with horizon: " << horizon << std::endl;
	auto forecast = model->predict(horizon);
	return std::make_unique<::anofoxtime::core::Forecast>(std::move(forecast));
}

std::unique_ptr<::anofoxtime::core::Forecast>
AnofoxTimeWrapper::PredictWithConfidence(::anofoxtime::models::IForecaster *model, int horizon,
                                         double confidence_level) {
	// std::cerr << "[DEBUG] AnofoxTimeWrapper::PredictWithConfidence with horizon: " << horizon
	//           << ", confidence: " << confidence_level << std::endl;

	// Try to dynamic_cast to models that support predictWithConfidence
	// We'll try the common models

	if (auto *naive = dynamic_cast<::anofoxtime::models::Naive *>(model)) {
		auto forecast = naive->predictWithConfidence(horizon, confidence_level);
		return std::make_unique<::anofoxtime::core::Forecast>(std::move(forecast));
	} else if (auto *seasonal_naive = dynamic_cast<::anofoxtime::models::SeasonalNaive *>(model)) {
		auto forecast = seasonal_naive->predictWithConfidence(horizon, confidence_level);
		return std::make_unique<::anofoxtime::core::Forecast>(std::move(forecast));
	} else if (auto *theta = dynamic_cast<::anofoxtime::models::Theta *>(model)) {
		auto forecast = theta->predictWithConfidence(horizon, confidence_level);
		return std::make_unique<::anofoxtime::core::Forecast>(std::move(forecast));
	} else if (auto *auto_arima = dynamic_cast<::anofoxtime::models::AutoARIMA *>(model)) {
		auto forecast = auto_arima->predictWithConfidence(horizon, confidence_level);
		return std::make_unique<::anofoxtime::core::Forecast>(std::move(forecast));
	} else {
		// Model doesn't support predictWithConfidence, fall back to regular predict
		// std::cerr << "[DEBUG] Model doesn't support predictWithConfidence, using predict()" << std::endl;
		auto forecast = model->predict(horizon);
		return std::make_unique<::anofoxtime::core::Forecast>(std::move(forecast));
	}
}

const std::vector<double> &AnofoxTimeWrapper::GetPrimaryForecast(const ::anofoxtime::core::Forecast &forecast) {
	return forecast.primary();
}

std::size_t AnofoxTimeWrapper::GetForecastHorizon(const ::anofoxtime::core::Forecast &forecast) {
	return forecast.horizon();
}

std::string AnofoxTimeWrapper::GetModelName(const ::anofoxtime::models::IForecaster &model) {
	return model.getName();
}

bool AnofoxTimeWrapper::HasLowerBound(const ::anofoxtime::core::Forecast &forecast) {
	try {
		const auto &lower = forecast.lowerSeries();
		return !lower.empty();
	} catch (...) {
		return false;
	}
}

bool AnofoxTimeWrapper::HasUpperBound(const ::anofoxtime::core::Forecast &forecast) {
	try {
		const auto &upper = forecast.upperSeries();
		return !upper.empty();
	} catch (...) {
		return false;
	}
}

const std::vector<double> &AnofoxTimeWrapper::GetLowerBound(const ::anofoxtime::core::Forecast &forecast) {
	return forecast.lowerSeries();
}

const std::vector<double> &AnofoxTimeWrapper::GetUpperBound(const ::anofoxtime::core::Forecast &forecast) {
	return forecast.upperSeries();
}

std::vector<double> AnofoxTimeWrapper::GetFittedValues(::anofoxtime::models::IForecaster *model) {
	// Use dynamic_cast to check if model supports fittedValues()
	// Only include models that have public fittedValues() method

#define TRY_GET_FITTED(ModelType)                                                                                      \
	if (auto *m = dynamic_cast<::anofoxtime::models::ModelType *>(model)) {                                            \
		return m->fittedValues();                                                                                      \
	}

	// Models with public fittedValues() method (confirmed by grep)
	TRY_GET_FITTED(Naive)
	TRY_GET_FITTED(SeasonalNaive)
	TRY_GET_FITTED(RandomWalkWithDrift)
	TRY_GET_FITTED(SESOptimized)
	TRY_GET_FITTED(HoltWinters)
	TRY_GET_FITTED(Theta)
	TRY_GET_FITTED(OptimizedTheta)
	TRY_GET_FITTED(DynamicTheta)
	TRY_GET_FITTED(DynamicOptimizedTheta)
	TRY_GET_FITTED(AutoTheta)
	TRY_GET_FITTED(SeasonalExponentialSmoothing)
	TRY_GET_FITTED(SeasonalESOptimized)
	TRY_GET_FITTED(SeasonalWindowAverage)
	TRY_GET_FITTED(ETS)
	TRY_GET_FITTED(AutoETS)
	TRY_GET_FITTED(AutoARIMA)
	TRY_GET_FITTED(MFLES)
	TRY_GET_FITTED(TBATS)
	TRY_GET_FITTED(CrostonClassic)
	TRY_GET_FITTED(CrostonOptimized)
	TRY_GET_FITTED(ADIDA)
	TRY_GET_FITTED(IMAPA)
	TRY_GET_FITTED(TSB)

#undef TRY_GET_FITTED

	// Model doesn't support fitted values - return empty vector
	return std::vector<double>();
}

} // namespace duckdb
