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
} // namespace core
namespace models {
class IForecaster;
// Basic models
class Naive;
class SeasonalNaive;
class SimpleMovingAverage;
class SimpleExponentialSmoothing;
class SESOptimized;
class RandomWalkWithDrift;
// Holt models
class HoltLinearTrend;
class HoltWinters;
// Theta variants
class Theta;
class OptimizedTheta;
class DynamicTheta;
class DynamicOptimizedTheta;
// Seasonal models
class SeasonalExponentialSmoothing;
class SeasonalESOptimized;
class SeasonalWindowAverage;
// ARIMA
class ARIMA;
class AutoARIMA;
// State space
class ETS;
class AutoETS;
// Multiple seasonality
class MFLES;
class AutoMFLES;
class MSTLForecaster;
class AutoMSTL;
class TBATS;
class AutoTBATS;
// Intermittent demand
class CrostonClassic;
class CrostonOptimized;
class CrostonSBA;
class ADIDA;
class IMAPA;
class TSB;
} // namespace models
} // namespace anofoxtime

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
	static std::unique_ptr<::anofoxtime::models::IForecaster>
	CreateHoltWinters(int seasonal_period, bool multiplicative, double alpha, double beta, double gamma);
#ifdef HAVE_EIGEN3
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateAutoARIMA(int seasonal_period);
#endif
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateETS(int error_type, int trend_type, int season_type,
	                                                                    int season_length, double alpha, double beta,
	                                                                    double gamma, double phi);
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateAutoETS(int season_length,
	                                                                        const std::string &model = "ZZZ");
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateMFLES(const std::vector<int> &seasonal_periods,
	                                                                      int n_iterations, double lr_trend,
	                                                                      double lr_season, double lr_level,
	                                                                      bool progressive_trend = true,
	                                                                      bool sequential_seasonality = true);
	static std::unique_ptr<::anofoxtime::models::IForecaster>
	CreateAutoMFLES(const std::vector<int> &seasonal_periods, int max_rounds = 10, double lr_trend = 0.3,
	                double lr_season = 0.5, double lr_rs = 0.8, int cv_horizon = -1, int cv_n_windows = 2,
	                ::anofoxtime::utils::CVMetric cv_metric = ::anofoxtime::utils::CVMetric::MAE);
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateMSTL(const std::vector<int> &seasonal_periods,
	                                                                     int trend_method, int seasonal_method);
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateAutoMSTL(const std::vector<int> &seasonal_periods);

	// Additional basic models
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateRandomWalkWithDrift();
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateSESOptimized();

#ifdef HAVE_EIGEN3
	// ARIMA (manual configuration)
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateARIMA(int p, int d, int q, int P, int D, int Q,
	                                                                      int s, bool include_intercept);
#endif

	// TBATS
	static std::unique_ptr<::anofoxtime::models::IForecaster>
	CreateTBATS(const std::vector<int> &seasonal_periods, bool use_box_cox, double box_cox_lambda, bool use_trend,
	            bool use_damped_trend, double damping_param, int ar_order, int ma_order);
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateAutoTBATS(const std::vector<int> &seasonal_periods);

	// Theta variants
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateOptimizedTheta(int seasonal_period);
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateDynamicTheta(int seasonal_period,
	                                                                             double theta_param);
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateDynamicOptimizedTheta(int seasonal_period);

	// Seasonal exponential smoothing
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateSeasonalES(int seasonal_period, double alpha,
	                                                                           double gamma);
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateSeasonalESOptimized(int seasonal_period);
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateSeasonalWindowAverage(int seasonal_period,
	                                                                                      int window);

	// Intermittent demand models
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateCrostonClassic();
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateCrostonOptimized();
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateCrostonSBA();
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateADIDA();
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateIMAPA();
	static std::unique_ptr<::anofoxtime::models::IForecaster> CreateTSB(double alpha_d, double alpha_p);

	// Build time series
	static std::unique_ptr<::anofoxtime::core::TimeSeries>
	BuildTimeSeries(const std::vector<std::chrono::system_clock::time_point> &timestamps,
	                const std::vector<double> &values);

	// Fit model
	static void FitModel(::anofoxtime::models::IForecaster *model, const ::anofoxtime::core::TimeSeries &ts);

	// Predict
	static std::unique_ptr<::anofoxtime::core::Forecast> Predict(::anofoxtime::models::IForecaster *model, int horizon);

	// Predict with confidence intervals (if supported by model)
	static std::unique_ptr<::anofoxtime::core::Forecast>
	PredictWithConfidence(::anofoxtime::models::IForecaster *model, int horizon, double confidence_level = 0.95);

	// Get forecast data
	static const std::vector<double> &GetPrimaryForecast(const ::anofoxtime::core::Forecast &forecast);
	static std::size_t GetForecastHorizon(const ::anofoxtime::core::Forecast &forecast);
	static std::string GetModelName(const ::anofoxtime::models::IForecaster &model);

	// Prediction interval accessors
	static bool HasLowerBound(const ::anofoxtime::core::Forecast &forecast);
	static bool HasUpperBound(const ::anofoxtime::core::Forecast &forecast);
	static const std::vector<double> &GetLowerBound(const ::anofoxtime::core::Forecast &forecast);
	static const std::vector<double> &GetUpperBound(const ::anofoxtime::core::Forecast &forecast);

	// Get fitted values (in-sample predictions)
	static std::vector<double> GetFittedValues(::anofoxtime::models::IForecaster *model);
};

} // namespace duckdb
