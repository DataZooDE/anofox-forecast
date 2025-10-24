// Define this BEFORE including any anofox-time headers
#define ANOFOX_NO_LOGGING

// This file isolates all anofox-time includes to prevent namespace conflicts
#include "anofox_time_wrapper.hpp"

// Include anofox-time headers
#include "anofox-time/models/sma.hpp"
#include "anofox-time/models/naive.hpp"
#include "anofox-time/models/seasonal_naive.hpp"
#include "anofox-time/models/ses.hpp"
#include "anofox-time/models/theta.hpp"
#include "anofox-time/models/holt.hpp"
#include "anofox-time/models/holt_winters.hpp"
#include "anofox-time/models/auto_arima.hpp"
#include "anofox-time/models/ets.hpp"
#include "anofox-time/models/auto_ets.hpp"
#include "anofox-time/models/mfles.hpp"
#include "anofox-time/models/auto_mfles.hpp"
#include "anofox-time/models/mstl_forecaster.hpp"
#include "anofox-time/models/auto_mstl.hpp"
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
    return ::anofoxtime::models::SimpleMovingAverageBuilder()
        .withWindow(window)
        .build();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateSES(double alpha) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateSES with alpha: " << alpha << std::endl;
    return ::anofoxtime::models::SimpleExponentialSmoothingBuilder()
        .withAlpha(alpha)
        .build();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateTheta(int seasonal_period, double theta_param) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateTheta with period: " << seasonal_period 
    //           << ", theta: " << theta_param << std::endl;
    return std::make_unique<::anofoxtime::models::Theta>(seasonal_period, theta_param);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateHolt(double alpha, double beta) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateHolt with alpha: " << alpha 
    //           << ", beta: " << beta << std::endl;
    return ::anofoxtime::models::HoltLinearTrendBuilder()
        .withAlpha(alpha)
        .withBeta(beta)
        .build();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateHoltWinters(
    int seasonal_period, bool multiplicative, double alpha, double beta, double gamma) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateHoltWinters with period: " << seasonal_period 
    //           << ", multiplicative: " << multiplicative 
    //           << ", alpha: " << alpha << ", beta: " << beta << ", gamma: " << gamma << std::endl;
    
    auto season_type = multiplicative ? 
        ::anofoxtime::models::HoltWinters::SeasonType::Multiplicative :
        ::anofoxtime::models::HoltWinters::SeasonType::Additive;
    
    return std::make_unique<::anofoxtime::models::HoltWinters>(
        seasonal_period, season_type, alpha, beta, gamma);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateAutoARIMA(int seasonal_period) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateAutoARIMA with period: " << seasonal_period << std::endl;
    return std::make_unique<::anofoxtime::models::AutoARIMA>(seasonal_period);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateETS(
    int error_type, int trend_type, int season_type, int season_length,
    double alpha, double beta, double gamma, double phi) {
    
    // Map integer types to enums
    ::anofoxtime::models::ETSErrorType error;
    switch (error_type) {
        case 0: error = ::anofoxtime::models::ETSErrorType::Additive; break;
        case 1: error = ::anofoxtime::models::ETSErrorType::Multiplicative; break;
        default: error = ::anofoxtime::models::ETSErrorType::Additive; break;
    }
    
    ::anofoxtime::models::ETSTrendType trend;
    switch (trend_type) {
        case 0: trend = ::anofoxtime::models::ETSTrendType::None; break;
        case 1: trend = ::anofoxtime::models::ETSTrendType::Additive; break;
        case 2: trend = ::anofoxtime::models::ETSTrendType::Multiplicative; break;
        case 3: trend = ::anofoxtime::models::ETSTrendType::DampedAdditive; break;
        case 4: trend = ::anofoxtime::models::ETSTrendType::DampedMultiplicative; break;
        default: trend = ::anofoxtime::models::ETSTrendType::None; break;
    }
    
    ::anofoxtime::models::ETSSeasonType season;
    switch (season_type) {
        case 0: season = ::anofoxtime::models::ETSSeasonType::None; break;
        case 1: season = ::anofoxtime::models::ETSSeasonType::Additive; break;
        case 2: season = ::anofoxtime::models::ETSSeasonType::Multiplicative; break;
        default: season = ::anofoxtime::models::ETSSeasonType::None; break;
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
    if (trend_type == 3 || trend_type == 4) {  // Damped trends
        builder.withPhi(phi);
    }
    
    return builder.build();
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateAutoETS(int season_length) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateAutoETS with season_length: " << season_length << std::endl;
    // "ZZZ" means auto-select all components
    return std::make_unique<::anofoxtime::models::AutoETS>(season_length, "ZZZ");
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateMFLES(
    const std::vector<int>& seasonal_periods, int n_iterations,
    double lr_trend, double lr_season, double lr_level) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateMFLES with " << seasonal_periods.size() << " periods" << std::endl;
    return std::make_unique<::anofoxtime::models::MFLES>(
        seasonal_periods, n_iterations, lr_trend, lr_season, lr_level);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateAutoMFLES(
    const std::vector<int>& seasonal_periods) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateAutoMFLES with " << seasonal_periods.size() << " periods" << std::endl;
    return std::make_unique<::anofoxtime::models::AutoMFLES>(seasonal_periods);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateMSTL(
    const std::vector<int>& seasonal_periods, int trend_method, int seasonal_method) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateMSTL" << std::endl;
    
    // Map integer to enum
    ::anofoxtime::models::MSTLForecaster::TrendMethod trend;
    switch (trend_method) {
        case 0: trend = ::anofoxtime::models::MSTLForecaster::TrendMethod::Linear; break;
        case 1: trend = ::anofoxtime::models::MSTLForecaster::TrendMethod::SES; break;
        case 2: trend = ::anofoxtime::models::MSTLForecaster::TrendMethod::Holt; break;
        case 3: trend = ::anofoxtime::models::MSTLForecaster::TrendMethod::None; break;
        default: trend = ::anofoxtime::models::MSTLForecaster::TrendMethod::Linear; break;
    }
    
    ::anofoxtime::models::MSTLForecaster::SeasonalMethod season;
    switch (seasonal_method) {
        case 0: season = ::anofoxtime::models::MSTLForecaster::SeasonalMethod::Cyclic; break;
        case 1: season = ::anofoxtime::models::MSTLForecaster::SeasonalMethod::AutoETSAdditive; break;
        case 2: season = ::anofoxtime::models::MSTLForecaster::SeasonalMethod::AutoETSMultiplicative; break;
        default: season = ::anofoxtime::models::MSTLForecaster::SeasonalMethod::Cyclic; break;
    }
    
    return std::make_unique<::anofoxtime::models::MSTLForecaster>(
        seasonal_periods, trend, season);
}

std::unique_ptr<::anofoxtime::models::IForecaster> AnofoxTimeWrapper::CreateAutoMSTL(
    const std::vector<int>& seasonal_periods) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::CreateAutoMSTL with " << seasonal_periods.size() << " periods" << std::endl;
    return std::make_unique<::anofoxtime::models::AutoMSTL>(seasonal_periods);
}

std::unique_ptr<::anofoxtime::core::TimeSeries> AnofoxTimeWrapper::BuildTimeSeries(
    const std::vector<std::chrono::system_clock::time_point>& timestamps,
    const std::vector<double>& values
) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::BuildTimeSeries with " << timestamps.size() << " points" << std::endl;
    
    std::vector<::anofoxtime::core::TimeSeries::TimePoint> ts_timestamps;
    ts_timestamps.reserve(timestamps.size());
    for (const auto& tp : timestamps) {
        ts_timestamps.push_back(tp);
    }
    
    return std::make_unique<::anofoxtime::core::TimeSeries>(
        std::move(ts_timestamps),
        values
    );
}

void AnofoxTimeWrapper::FitModel(
    ::anofoxtime::models::IForecaster* model,
    const ::anofoxtime::core::TimeSeries& ts
) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::FitModel" << std::endl;
    model->fit(ts);
}

std::unique_ptr<::anofoxtime::core::Forecast> AnofoxTimeWrapper::Predict(
    ::anofoxtime::models::IForecaster* model,
    int horizon
) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::Predict with horizon: " << horizon << std::endl;
    auto forecast = model->predict(horizon);
    return std::make_unique<::anofoxtime::core::Forecast>(std::move(forecast));
}

std::unique_ptr<::anofoxtime::core::Forecast> AnofoxTimeWrapper::PredictWithConfidence(
    ::anofoxtime::models::IForecaster* model,
    int horizon,
    double confidence_level
) {
    // std::cerr << "[DEBUG] AnofoxTimeWrapper::PredictWithConfidence with horizon: " << horizon 
    //           << ", confidence: " << confidence_level << std::endl;
    
    // Try to dynamic_cast to models that support predictWithConfidence
    // We'll try the common models
    
    if (auto* naive = dynamic_cast<::anofoxtime::models::Naive*>(model)) {
        auto forecast = naive->predictWithConfidence(horizon, confidence_level);
        return std::make_unique<::anofoxtime::core::Forecast>(std::move(forecast));
    }
    else if (auto* seasonal_naive = dynamic_cast<::anofoxtime::models::SeasonalNaive*>(model)) {
        auto forecast = seasonal_naive->predictWithConfidence(horizon, confidence_level);
        return std::make_unique<::anofoxtime::core::Forecast>(std::move(forecast));
    }
    else if (auto* theta = dynamic_cast<::anofoxtime::models::Theta*>(model)) {
        auto forecast = theta->predictWithConfidence(horizon, confidence_level);
        return std::make_unique<::anofoxtime::core::Forecast>(std::move(forecast));
    }
    else if (auto* auto_arima = dynamic_cast<::anofoxtime::models::AutoARIMA*>(model)) {
        auto forecast = auto_arima->predictWithConfidence(horizon, confidence_level);
        return std::make_unique<::anofoxtime::core::Forecast>(std::move(forecast));
    }
    else {
        // Model doesn't support predictWithConfidence, fall back to regular predict
        // std::cerr << "[DEBUG] Model doesn't support predictWithConfidence, using predict()" << std::endl;
        auto forecast = model->predict(horizon);
        return std::make_unique<::anofoxtime::core::Forecast>(std::move(forecast));
    }
}

const std::vector<double>& AnofoxTimeWrapper::GetPrimaryForecast(const ::anofoxtime::core::Forecast& forecast) {
    return forecast.primary();
}

std::size_t AnofoxTimeWrapper::GetForecastHorizon(const ::anofoxtime::core::Forecast& forecast) {
    return forecast.horizon();
}

std::string AnofoxTimeWrapper::GetModelName(const ::anofoxtime::models::IForecaster& model) {
    return model.getName();
}

bool AnofoxTimeWrapper::HasLowerBound(const ::anofoxtime::core::Forecast& forecast) {
    try {
        const auto& lower = forecast.lowerSeries();
        return !lower.empty();
    } catch (...) {
        return false;
    }
}

bool AnofoxTimeWrapper::HasUpperBound(const ::anofoxtime::core::Forecast& forecast) {
    try {
        const auto& upper = forecast.upperSeries();
        return !upper.empty();
    } catch (...) {
        return false;
    }
}

const std::vector<double>& AnofoxTimeWrapper::GetLowerBound(const ::anofoxtime::core::Forecast& forecast) {
    return forecast.lowerSeries();
}

const std::vector<double>& AnofoxTimeWrapper::GetUpperBound(const ::anofoxtime::core::Forecast& forecast) {
    return forecast.upperSeries();
}

} // namespace duckdb