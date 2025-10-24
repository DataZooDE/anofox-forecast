#pragma once

// Forward declarations to avoid including full anofox-time headers
#include <memory>
#include <vector>
#include <chrono>
#include <string>

// Forward declare anofoxtime types in global namespace
namespace anofoxtime {
namespace core {
    class TimeSeries;
    struct Forecast;
}
namespace models {
    class IForecaster;
    class Naive;
    class SeasonalNaive;
    class SimpleMovingAverage;
    class SimpleExponentialSmoothing;
    class Theta;
    class HoltLinearTrend;
    class HoltWinters;
    class AutoARIMA;
    class ETS;
    class AutoETS;
    class MFLES;
    class AutoMFLES;
    class MSTLForecaster;
    class AutoMSTL;
}
}

namespace duckdb {

// Wrapper functions that isolate anofox-time types
class AnofoxTimeWrapper {
public:
    // Create models (returns in global anofoxtime namespace)
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateNaive();
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateSeasonalNaive(int period);
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateSMA(int window);
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateSES(double alpha);
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateTheta(int seasonal_period, double theta_param);
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateHolt(double alpha, double beta);
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateHoltWinters(
        int seasonal_period, bool multiplicative, double alpha, double beta, double gamma);
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateAutoARIMA(int seasonal_period);
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateETS(
        int error_type, int trend_type, int season_type, int season_length, 
        double alpha, double beta, double gamma, double phi);
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateAutoETS(int season_length);
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateMFLES(
        const std::vector<int>& seasonal_periods, int n_iterations,
        double lr_trend, double lr_season, double lr_level);
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateAutoMFLES(
        const std::vector<int>& seasonal_periods);
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateMSTL(
        const std::vector<int>& seasonal_periods, int trend_method, int seasonal_method);
    static std::unique_ptr<::anofoxtime::models::IForecaster> CreateAutoMSTL(
        const std::vector<int>& seasonal_periods);
    
    // Build time series
    static std::unique_ptr<::anofoxtime::core::TimeSeries> BuildTimeSeries(
        const std::vector<std::chrono::system_clock::time_point>& timestamps,
        const std::vector<double>& values
    );
    
    // Fit model
    static void FitModel(
        ::anofoxtime::models::IForecaster* model,
        const ::anofoxtime::core::TimeSeries& ts
    );
    
    // Predict
    static std::unique_ptr<::anofoxtime::core::Forecast> Predict(
        ::anofoxtime::models::IForecaster* model,
        int horizon
    );
    
    // Predict with confidence intervals (if supported by model)
    static std::unique_ptr<::anofoxtime::core::Forecast> PredictWithConfidence(
        ::anofoxtime::models::IForecaster* model,
        int horizon,
        double confidence_level = 0.95
    );
    
    // Get forecast data
    static const std::vector<double>& GetPrimaryForecast(const ::anofoxtime::core::Forecast& forecast);
    static std::size_t GetForecastHorizon(const ::anofoxtime::core::Forecast& forecast);
    static std::string GetModelName(const ::anofoxtime::models::IForecaster& model);
    
    // Prediction interval accessors
    static bool HasLowerBound(const ::anofoxtime::core::Forecast& forecast);
    static bool HasUpperBound(const ::anofoxtime::core::Forecast& forecast);
    static const std::vector<double>& GetLowerBound(const ::anofoxtime::core::Forecast& forecast);
    static const std::vector<double>& GetUpperBound(const ::anofoxtime::core::Forecast& forecast);
};

} // namespace duckdb