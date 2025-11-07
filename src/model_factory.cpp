#include "model_factory.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <iostream>

// Need to include full types for unique_ptr destructor
#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/method_name_wrapper.hpp"

namespace duckdb {

std::unique_ptr<::anofoxtime::models::IForecaster> ModelFactory::Create(const std::string &model_name,
                                                                        const Value &model_params) {
	// std::cerr << "[DEBUG] ModelFactory::Create called with model: " << model_name << std::endl;

	// Validate parameters first
	ValidateModelParams(model_name, model_params);

	// Create the base model
	std::unique_ptr<::anofoxtime::models::IForecaster> model;

	if (model_name == "SMA") {
		int window = GetParam<int>(model_params, "window", 5);
		// std::cerr << "[DEBUG] Creating SMA model with window: " << window << std::endl;
		model = AnofoxTimeWrapper::CreateSMA(window);
	} else if (model_name == "Naive") {
		// std::cerr << "[DEBUG] Creating Naive model" << std::endl;
		model = AnofoxTimeWrapper::CreateNaive();
	} else if (model_name == "SeasonalNaive") {
		int period = GetRequiredParam<int>(model_params, "seasonal_period");
		// std::cerr << "[DEBUG] Creating SeasonalNaive model with period: " << period << std::endl;
		model = AnofoxTimeWrapper::CreateSeasonalNaive(period);
	} else if (model_name == "SES") {
		double alpha = GetParam<double>(model_params, "alpha", 0.3);
		// std::cerr << "[DEBUG] Creating SES model with alpha: " << alpha << std::endl;
		model = AnofoxTimeWrapper::CreateSES(alpha);
	} else if (model_name == "Theta") {
		int seasonal_period = GetParam<int>(model_params, "seasonal_period", 1);
		double theta_param = GetParam<double>(model_params, "theta", 2.0);
		// std::cerr << "[DEBUG] Creating Theta model with period: " << seasonal_period
		//           << ", theta: " << theta_param << std::endl;
		model = AnofoxTimeWrapper::CreateTheta(seasonal_period, theta_param);
	} else if (model_name == "Holt") {
		double alpha = GetParam<double>(model_params, "alpha", 0.3);
		double beta = GetParam<double>(model_params, "beta", 0.1);
		// std::cerr << "[DEBUG] Creating Holt model with alpha: " << alpha
		//           << ", beta: " << beta << std::endl;
		model = AnofoxTimeWrapper::CreateHolt(alpha, beta);
	} else if (model_name == "HoltWinters") {
		int seasonal_period = GetRequiredParam<int>(model_params, "seasonal_period");
		bool multiplicative = GetParam<int>(model_params, "multiplicative", 0) != 0;
		double alpha = GetParam<double>(model_params, "alpha", 0.2);
		double beta = GetParam<double>(model_params, "beta", 0.1);
		double gamma = GetParam<double>(model_params, "gamma", 0.1);
		// std::cerr << "[DEBUG] Creating HoltWinters model with period: " << seasonal_period
		//           << ", multiplicative: " << multiplicative << std::endl;
		model = AnofoxTimeWrapper::CreateHoltWinters(seasonal_period, multiplicative, alpha, beta, gamma);
	}
#ifdef HAVE_EIGEN3
	else if (model_name == "AutoARIMA") {
		int seasonal_period = GetParam<int>(model_params, "seasonal_period", 0);
		// std::cerr << "[DEBUG] Creating AutoARIMA model with period: " << seasonal_period << std::endl;
		model = AnofoxTimeWrapper::CreateAutoARIMA(seasonal_period);
	}
#endif
	else if (model_name == "ETS") {
		int error_type = GetParam<int>(model_params, "error_type", 0);   // 0=Additive
		int trend_type = GetParam<int>(model_params, "trend_type", 0);   // 0=None
		int season_type = GetParam<int>(model_params, "season_type", 0); // 0=None
		// Accept both 'season_length' and 'seasonal_period' for compatibility
		int season_length = HasParam(model_params, "season_length") ? GetParam<int>(model_params, "season_length", 1)
		                                                            : GetParam<int>(model_params, "seasonal_period", 1);
		double alpha = GetParam<double>(model_params, "alpha", 0.2);
		double beta = GetParam<double>(model_params, "beta", 0.1);
		double gamma = GetParam<double>(model_params, "gamma", 0.1);
		double phi = GetParam<double>(model_params, "phi", 0.98);
		// std::cerr << "[DEBUG] Creating ETS model with error:" << error_type
		//           << ", trend:" << trend_type << ", season:" << season_type << std::endl;
		model = AnofoxTimeWrapper::CreateETS(error_type, trend_type, season_type, season_length, alpha, beta, gamma,
		                                    phi);
	} else if (model_name == "AutoETS") {
		// Accept both 'season_length' and 'seasonal_period' for compatibility
		int season_length = HasParam(model_params, "season_length") ? GetParam<int>(model_params, "season_length", 1)
		                                                            : GetParam<int>(model_params, "seasonal_period", 1);
		std::string model_spec = GetParam<std::string>(model_params, "model", "ZZZ");

		model = AnofoxTimeWrapper::CreateAutoETS(season_length, model_spec);
	} else if (model_name == "MFLES") {
		// Check both 'seasonal_periods' (plural) and 'seasonal_period' (singular)
		std::vector<int> seasonal_periods;
		if (HasParam(model_params, "seasonal_periods")) {
			seasonal_periods = GetArrayParam(model_params, "seasonal_periods", {12});
		} else if (HasParam(model_params, "seasonal_period")) {
			int single_period = GetParam<int>(model_params, "seasonal_period", 12);
			seasonal_periods = {single_period};
		} else {
			seasonal_periods = {12};
		}
		// Tuned defaults for best accuracy (9% error vs statsforecast)
		int n_iterations = GetParam<int>(model_params, "n_iterations", 10);
		double lr_trend = GetParam<double>(model_params, "lr_trend", 0.3);
		double lr_season = GetParam<double>(model_params, "lr_season", 0.5);
		double lr_level = GetParam<double>(model_params, "lr_level", 0.8);
		bool progressive_trend = GetParam<bool>(model_params, "progressive_trend", true);
		bool sequential_seasonality = GetParam<bool>(model_params, "sequential_seasonality", true);
		model = AnofoxTimeWrapper::CreateMFLES(seasonal_periods, n_iterations, lr_trend, lr_season, lr_level,
		                                      progressive_trend, sequential_seasonality);
	} else if (model_name == "AutoMFLES") {
		// Check both 'seasonal_periods' (plural) and 'seasonal_period' (singular)
		std::vector<int> seasonal_periods;
		if (HasParam(model_params, "seasonal_periods")) {
			seasonal_periods = GetArrayParam(model_params, "seasonal_periods", {12});
		} else if (HasParam(model_params, "seasonal_period")) {
			int single_period = GetParam<int>(model_params, "seasonal_period", 12);
			seasonal_periods = {single_period};
		} else {
			seasonal_periods = {12};
		}
		// User-configurable parameters with tuned defaults
		int max_rounds = GetParam<int>(model_params, "max_rounds", 10);
		double lr_trend = GetParam<double>(model_params, "lr_trend", 0.3);
		double lr_season = GetParam<double>(model_params, "lr_season", 0.5);
		double lr_rs = GetParam<double>(model_params, "lr_rs", 0.8);
		int cv_horizon = GetParam<int>(model_params, "cv_horizon", -1);
		int cv_n_windows = GetParam<int>(model_params, "cv_n_windows", 2);
		
		// Parse optimization metric
		std::string metric_str = GetParam<std::string>(model_params, "metric", "mae");
		::anofoxtime::utils::CVMetric metric;
		if (metric_str == "mae") {
			metric = ::anofoxtime::utils::CVMetric::MAE;
		} else if (metric_str == "rmse") {
			metric = ::anofoxtime::utils::CVMetric::RMSE;
		} else if (metric_str == "mape") {
			metric = ::anofoxtime::utils::CVMetric::MAPE;
		} else if (metric_str == "smape") {
			metric = ::anofoxtime::utils::CVMetric::SMAPE;
		} else {
			throw InvalidInputException("Invalid metric: " + metric_str + 
				". Must be one of: mae, rmse, mape, smape");
		}
		
		model = AnofoxTimeWrapper::CreateAutoMFLES(seasonal_periods, max_rounds, lr_trend, lr_season, lr_rs, cv_horizon,
		                                          cv_n_windows, metric);
	} else if (model_name == "MSTL") {
		// Check both 'seasonal_periods' (plural) and 'seasonal_period' (singular)
		std::vector<int> seasonal_periods;
		if (HasParam(model_params, "seasonal_periods")) {
			seasonal_periods = GetArrayParam(model_params, "seasonal_periods", {12});
		} else if (HasParam(model_params, "seasonal_period")) {
			int single_period = GetParam<int>(model_params, "seasonal_period", 12);
			seasonal_periods = {single_period};
		} else {
			seasonal_periods = {12};
		}
		int trend_method = GetParam<int>(model_params, "trend_method", 0);       // 0=Linear
		int seasonal_method = GetParam<int>(model_params, "seasonal_method", 0); // 0=Cyclic
		// std::cerr << "[DEBUG] Creating MSTL model" << std::endl;
		model = AnofoxTimeWrapper::CreateMSTL(seasonal_periods, trend_method, seasonal_method);
	} else if (model_name == "AutoMSTL") {
		// Check both 'seasonal_periods' (plural) and 'seasonal_period' (singular)
		std::vector<int> seasonal_periods;
		if (HasParam(model_params, "seasonal_periods")) {
			seasonal_periods = GetArrayParam(model_params, "seasonal_periods", {12});
		} else if (HasParam(model_params, "seasonal_period")) {
			int single_period = GetParam<int>(model_params, "seasonal_period", 12);
			seasonal_periods = {single_period};
		} else {
			seasonal_periods = {12};
		}
		// std::cerr << "[DEBUG] Creating AutoMSTL model" << std::endl;
		model = AnofoxTimeWrapper::CreateAutoMSTL(seasonal_periods);
	}
	// Additional basic models
	else if (model_name == "RandomWalkWithDrift") {
		model = AnofoxTimeWrapper::CreateRandomWalkWithDrift();
	} else if (model_name == "SESOptimized") {
		model = AnofoxTimeWrapper::CreateSESOptimized();
	}
#ifdef HAVE_EIGEN3
	// ARIMA manual
	else if (model_name == "ARIMA") {
		int p = GetParam<int>(model_params, "p", 1);
		int d = GetParam<int>(model_params, "d", 0);
		int q = GetParam<int>(model_params, "q", 0);
		int P = GetParam<int>(model_params, "P", 0);
		int D = GetParam<int>(model_params, "D", 0);
		int Q = GetParam<int>(model_params, "Q", 0);
		int s = GetParam<int>(model_params, "s", 0);
		bool include_intercept = GetParam<int>(model_params, "include_intercept", 1) != 0;
		model = AnofoxTimeWrapper::CreateARIMA(p, d, q, P, D, Q, s, include_intercept);
	}
#endif
	// TBATS
	else if (model_name == "TBATS") {
		// Check both 'seasonal_periods' (plural) and 'seasonal_period' (singular)
		std::vector<int> seasonal_periods;
		if (HasParam(model_params, "seasonal_periods")) {
			seasonal_periods = GetArrayParam(model_params, "seasonal_periods", {12});
		} else if (HasParam(model_params, "seasonal_period")) {
			int single_period = GetParam<int>(model_params, "seasonal_period", 12);
			seasonal_periods = {single_period};
		} else {
			seasonal_periods = {12};
		}
		bool use_box_cox = GetParam<int>(model_params, "use_box_cox", 0) != 0;
		double box_cox_lambda = GetParam<double>(model_params, "box_cox_lambda", 1.0);
		bool use_trend = GetParam<int>(model_params, "use_trend", 1) != 0;
		bool use_damped_trend = GetParam<int>(model_params, "use_damped_trend", 0) != 0;
		double damping_param = GetParam<double>(model_params, "damping_param", 0.98);
		int ar_order = GetParam<int>(model_params, "ar_order", 0);
		int ma_order = GetParam<int>(model_params, "ma_order", 0);
		model = AnofoxTimeWrapper::CreateTBATS(seasonal_periods, use_box_cox, box_cox_lambda, use_trend,
		                                      use_damped_trend, damping_param, ar_order, ma_order);
	} else if (model_name == "AutoTBATS") {
		// Check both 'seasonal_periods' (plural) and 'seasonal_period' (singular)
		std::vector<int> seasonal_periods;
		if (HasParam(model_params, "seasonal_periods")) {
			seasonal_periods = GetArrayParam(model_params, "seasonal_periods", {12});
		} else if (HasParam(model_params, "seasonal_period")) {
			int single_period = GetParam<int>(model_params, "seasonal_period", 12);
			seasonal_periods = {single_period};
		} else {
			seasonal_periods = {12};
		}
		model = AnofoxTimeWrapper::CreateAutoTBATS(seasonal_periods);
	}
	// Theta variants
	else if (model_name == "OptimizedTheta") {
		int seasonal_period = GetParam<int>(model_params, "seasonal_period", 1);
		model = AnofoxTimeWrapper::CreateOptimizedTheta(seasonal_period);
	} else if (model_name == "DynamicTheta") {
		int seasonal_period = GetParam<int>(model_params, "seasonal_period", 1);
		double theta_param = GetParam<double>(model_params, "theta", 2.0);
		model = AnofoxTimeWrapper::CreateDynamicTheta(seasonal_period, theta_param);
	} else if (model_name == "DynamicOptimizedTheta") {
		int seasonal_period = GetParam<int>(model_params, "seasonal_period", 1);
		model = AnofoxTimeWrapper::CreateDynamicOptimizedTheta(seasonal_period);
	}
	// Seasonal exponential smoothing
	else if (model_name == "SeasonalES") {
		int seasonal_period = GetRequiredParam<int>(model_params, "seasonal_period");
		double alpha = GetParam<double>(model_params, "alpha", 0.2);
		double gamma = GetParam<double>(model_params, "gamma", 0.1);
		model = AnofoxTimeWrapper::CreateSeasonalES(seasonal_period, alpha, gamma);
	} else if (model_name == "SeasonalESOptimized") {
		int seasonal_period = GetRequiredParam<int>(model_params, "seasonal_period");
		model = AnofoxTimeWrapper::CreateSeasonalESOptimized(seasonal_period);
	} else if (model_name == "SeasonalWindowAverage") {
		int seasonal_period = GetRequiredParam<int>(model_params, "seasonal_period");
		int window = GetParam<int>(model_params, "window", 5);
		model = AnofoxTimeWrapper::CreateSeasonalWindowAverage(seasonal_period, window);
	}
	// Intermittent demand models
	else if (model_name == "CrostonClassic") {
		model = AnofoxTimeWrapper::CreateCrostonClassic();
	} else if (model_name == "CrostonOptimized") {
		model = AnofoxTimeWrapper::CreateCrostonOptimized();
	} else if (model_name == "CrostonSBA") {
		model = AnofoxTimeWrapper::CreateCrostonSBA();
	} else if (model_name == "ADIDA") {
		model = AnofoxTimeWrapper::CreateADIDA();
	} else if (model_name == "IMAPA") {
		model = AnofoxTimeWrapper::CreateIMAPA();
	} else if (model_name == "TSB") {
		double alpha_d = GetParam<double>(model_params, "alpha_d", 0.1);
		double alpha_p = GetParam<double>(model_params, "alpha_p", 0.1);
		model = AnofoxTimeWrapper::CreateTSB(alpha_d, alpha_p);
	} else {
		auto supported_models = GetSupportedModels();
		std::string supported_list;
		for (size_t i = 0; i < supported_models.size(); i++) {
			if (i > 0)
				supported_list += ", ";
			supported_list += supported_models[i];
		}
		throw InvalidInputException("Unknown model: '" + model_name + "'. Supported models: " + supported_list);
	}

	// Apply method_name wrapper if custom name was provided
	if (HasParam(model_params, "method_name")) {
		std::string custom_name = GetParam<std::string>(model_params, "method_name", "");
		if (!custom_name.empty()) {
			model = std::make_unique<::anofoxtime::models::MethodNameWrapper>(
				std::move(model), custom_name);
		}
	}

	return model;
}

std::vector<std::string> ModelFactory::GetSupportedModels() {
	std::vector<std::string> models = {// Basic
	                                   "Naive", "SMA", "SeasonalNaive", "SES", "SESOptimized", "RandomWalkWithDrift",
	                                   // Holt
	                                   "Holt", "HoltWinters",
	                                   // Theta variants
	                                   "Theta", "OptimizedTheta", "DynamicTheta", "DynamicOptimizedTheta",
	                                   // Seasonal
	                                   "SeasonalES", "SeasonalESOptimized", "SeasonalWindowAverage",
#ifdef HAVE_EIGEN3
	                                   // ARIMA (requires Eigen3)
	                                   "ARIMA", "AutoARIMA",
#endif
	                                   // State space
	                                   "ETS", "AutoETS",
	                                   // Multiple seasonality
	                                   "MFLES", "AutoMFLES", "MSTL", "AutoMSTL", "TBATS", "AutoTBATS",
	                                   // Intermittent demand
	                                   "CrostonClassic", "CrostonOptimized", "CrostonSBA", "ADIDA", "IMAPA", "TSB"};
	return models;
}

void ModelFactory::ValidateModelParams(const std::string &model_name, const Value &model_params) {
	// std::cerr << "[DEBUG] Validating parameters for model: " << model_name << std::endl;

	if (model_name == "SMA") {
		// window is optional, default to 5
		if (HasParam(model_params, "window")) {
			int window = GetParam<int>(model_params, "window", 5);
			if (window <= 0) {
				throw InvalidInputException("SMA window parameter must be positive, got: " + std::to_string(window));
			}
		}
	} else if (model_name == "Naive") {
		// No parameters required
	} else if (model_name == "SeasonalNaive") {
		// seasonal_period is required
		if (!HasParam(model_params, "seasonal_period")) {
			throw InvalidInputException("SeasonalNaive model requires 'seasonal_period' parameter");
		}
		int period = GetRequiredParam<int>(model_params, "seasonal_period");
		if (period <= 0) {
			throw InvalidInputException("SeasonalNaive seasonal_period must be positive, got: " +
			                            std::to_string(period));
		}
	} else if (model_name == "SES") {
		// alpha is optional with default 0.3
		if (HasParam(model_params, "alpha")) {
			double alpha = GetParam<double>(model_params, "alpha", 0.3);
			if (alpha <= 0.0 || alpha > 1.0) {
				throw InvalidInputException("SES alpha parameter must be in (0, 1], got: " + std::to_string(alpha));
			}
		}
	} else if (model_name == "Theta") {
		// Both parameters are optional
		if (HasParam(model_params, "seasonal_period")) {
			int period = GetParam<int>(model_params, "seasonal_period", 1);
			if (period <= 0) {
				throw InvalidInputException("Theta seasonal_period must be positive, got: " + std::to_string(period));
			}
		}
		if (HasParam(model_params, "theta")) {
			double theta = GetParam<double>(model_params, "theta", 2.0);
			if (theta <= 0.0) {
				throw InvalidInputException("Theta parameter must be positive, got: " + std::to_string(theta));
			}
		}
	} else if (model_name == "Holt") {
		// Both alpha and beta are optional
		if (HasParam(model_params, "alpha")) {
			double alpha = GetParam<double>(model_params, "alpha", 0.3);
			if (alpha <= 0.0 || alpha > 1.0) {
				throw InvalidInputException("Holt alpha must be in (0, 1], got: " + std::to_string(alpha));
			}
		}
		if (HasParam(model_params, "beta")) {
			double beta = GetParam<double>(model_params, "beta", 0.1);
			if (beta < 0.0 || beta > 1.0) {
				throw InvalidInputException("Holt beta must be in [0, 1], got: " + std::to_string(beta));
			}
		}
	} else if (model_name == "HoltWinters") {
		// seasonal_period is required
		if (!HasParam(model_params, "seasonal_period")) {
			throw InvalidInputException("HoltWinters requires 'seasonal_period' parameter");
		}
		int period = GetRequiredParam<int>(model_params, "seasonal_period");
		if (period <= 1) {
			throw InvalidInputException("HoltWinters seasonal_period must be > 1, got: " + std::to_string(period));
		}
		// Validate optional smoothing parameters
		if (HasParam(model_params, "alpha")) {
			double alpha = GetParam<double>(model_params, "alpha", 0.2);
			if (alpha <= 0.0 || alpha > 1.0) {
				throw InvalidInputException("HoltWinters alpha must be in (0, 1], got: " + std::to_string(alpha));
			}
		}
		if (HasParam(model_params, "beta")) {
			double beta = GetParam<double>(model_params, "beta", 0.1);
			if (beta < 0.0 || beta > 1.0) {
				throw InvalidInputException("HoltWinters beta must be in [0, 1], got: " + std::to_string(beta));
			}
		}
		if (HasParam(model_params, "gamma")) {
			double gamma = GetParam<double>(model_params, "gamma", 0.1);
			if (gamma < 0.0 || gamma > 1.0) {
				throw InvalidInputException("HoltWinters gamma must be in [0, 1], got: " + std::to_string(gamma));
			}
		}
	}
#ifdef HAVE_EIGEN3
	else if (model_name == "AutoARIMA") {
		// seasonal_period is optional, defaults to 0 (non-seasonal)
		if (HasParam(model_params, "seasonal_period")) {
			int period = GetParam<int>(model_params, "seasonal_period", 0);
			if (period < 0) {
				throw InvalidInputException("AutoARIMA seasonal_period must be non-negative, got: " +
				                            std::to_string(period));
			}
		}
	}
#endif
	else if (model_name == "ETS") {
		// All parameters are optional with defaults
		if (HasParam(model_params, "error_type")) {
			int error_type = GetParam<int>(model_params, "error_type", 0);
			if (error_type < 0 || error_type > 1) {
				throw InvalidInputException("ETS error_type must be 0 (Additive) or 1 (Multiplicative), got: " +
				                            std::to_string(error_type));
			}
		}
		if (HasParam(model_params, "trend_type")) {
			int trend_type = GetParam<int>(model_params, "trend_type", 0);
			if (trend_type < 0 || trend_type > 4) {
				throw InvalidInputException("ETS trend_type must be 0-4, got: " + std::to_string(trend_type));
			}
		}
		if (HasParam(model_params, "season_type")) {
			int season_type = GetParam<int>(model_params, "season_type", 0);
			if (season_type < 0 || season_type > 2) {
				throw InvalidInputException("ETS season_type must be 0-2, got: " + std::to_string(season_type));
			}
		}
		if (HasParam(model_params, "season_length")) {
			int season_length = GetParam<int>(model_params, "season_length", 1);
			if (season_length < 1) {
				throw InvalidInputException("ETS season_length must be >= 1, got: " + std::to_string(season_length));
			}
		}
		if (HasParam(model_params, "alpha")) {
			double alpha = GetParam<double>(model_params, "alpha", 0.2);
			if (alpha <= 0.0 || alpha > 1.0) {
				throw InvalidInputException("ETS alpha must be in (0, 1], got: " + std::to_string(alpha));
			}
		}
		if (HasParam(model_params, "beta")) {
			double beta = GetParam<double>(model_params, "beta", 0.1);
			if (beta < 0.0 || beta > 1.0) {
				throw InvalidInputException("ETS beta must be in [0, 1], got: " + std::to_string(beta));
			}
		}
		if (HasParam(model_params, "gamma")) {
			double gamma = GetParam<double>(model_params, "gamma", 0.1);
			if (gamma < 0.0 || gamma > 1.0) {
				throw InvalidInputException("ETS gamma must be in [0, 1], got: " + std::to_string(gamma));
			}
		}
		if (HasParam(model_params, "phi")) {
			double phi = GetParam<double>(model_params, "phi", 0.98);
			if (phi <= 0.0 || phi > 1.0) {
				throw InvalidInputException("ETS phi must be in (0, 1], got: " + std::to_string(phi));
			}
		}
	} else if (model_name == "AutoETS") {
		// season_length is optional with default 1
		if (HasParam(model_params, "season_length")) {
			int season_length = GetParam<int>(model_params, "season_length", 1);
			if (season_length < 1) {
				throw InvalidInputException("AutoETS season_length must be >= 1, got: " +
				                            std::to_string(season_length));
			}
		}
		// model specification is optional with default "ZZZ"
		if (HasParam(model_params, "model")) {
			// Validate model spec format (3 or 4 characters)
			std::string model_spec = GetParam<std::string>(model_params, "model", "ZZZ");
			if (model_spec.size() != 3 && model_spec.size() != 4) {
				throw InvalidInputException(
				    "AutoETS model specification must be 3 or 4 characters (e.g., 'AAA', 'AAdA', 'ZZN'), got: " +
				    model_spec);
			}
		}
	} else if (model_name == "MFLES") {
		// All parameters optional with defaults
		if (HasParam(model_params, "n_iterations")) {
			int n_iter = GetParam<int>(model_params, "n_iterations", 10);
			if (n_iter < 1) {
				throw InvalidInputException("MFLES n_iterations must be >= 1, got: " + std::to_string(n_iter));
			}
		}
		// Learning rates should be in (0, 1]
		if (HasParam(model_params, "lr_trend")) {
			double lr = GetParam<double>(model_params, "lr_trend", 0.3);
			if (lr <= 0.0 || lr > 1.0) {
				throw InvalidInputException("MFLES lr_trend must be in (0, 1], got: " + std::to_string(lr));
			}
		}
		if (HasParam(model_params, "lr_season")) {
			double lr = GetParam<double>(model_params, "lr_season", 0.5);
			if (lr <= 0.0 || lr > 1.0) {
				throw InvalidInputException("MFLES lr_season must be in (0, 1], got: " + std::to_string(lr));
			}
		}
		if (HasParam(model_params, "lr_level")) {
			double lr = GetParam<double>(model_params, "lr_level", 0.8);
			if (lr <= 0.0 || lr > 1.0) {
				throw InvalidInputException("MFLES lr_level must be in (0, 1], got: " + std::to_string(lr));
			}
		}
	} else if (model_name == "AutoMFLES") {
		// seasonal_periods is optional
	} else if (model_name == "MSTL") {
		// All parameters optional
		if (HasParam(model_params, "trend_method")) {
			int method = GetParam<int>(model_params, "trend_method", 0);
			if (method < 0 || method > 5) {
				throw InvalidInputException("MSTL trend_method must be 0-5 (0=Linear, 1=SES, 2=Holt, 3=None, 4=AutoETS "
				                            "Additive, 5=AutoETS Multiplicative), got: " +
				                            std::to_string(method));
			}
		}
		if (HasParam(model_params, "seasonal_method")) {
			int method = GetParam<int>(model_params, "seasonal_method", 0);
			if (method < 0 || method > 2) {
				throw InvalidInputException("MSTL seasonal_method must be 0-2, got: " + std::to_string(method));
			}
		}
	} else if (model_name == "AutoMSTL") {
		// seasonal_periods is optional
	}
	// Additional basic models (no parameters)
	else if (model_name == "RandomWalkWithDrift" || model_name == "SESOptimized") {
		// No parameters
	}
#ifdef HAVE_EIGEN3
	// ARIMA manual
	else if (model_name == "ARIMA") {
		// All parameters are optional with defaults, validate ranges
		if (HasParam(model_params, "p") && GetParam<int>(model_params, "p", 1) < 0) {
			throw InvalidInputException("ARIMA p must be non-negative");
		}
		if (HasParam(model_params, "d") && GetParam<int>(model_params, "d", 0) < 0) {
			throw InvalidInputException("ARIMA d must be non-negative");
		}
		if (HasParam(model_params, "q") && GetParam<int>(model_params, "q", 0) < 0) {
			throw InvalidInputException("ARIMA q must be non-negative");
		}
	}
#endif
	// TBATS
	else if (model_name == "TBATS") {
		// All parameters optional
	} else if (model_name == "AutoTBATS") {
		// seasonal_periods is optional
	}
	// Theta variants
	else if (model_name == "OptimizedTheta" || model_name == "DynamicTheta" || model_name == "DynamicOptimizedTheta") {
		// seasonal_period is optional
	}
	// Seasonal exponential smoothing
	else if (model_name == "SeasonalES") {
		if (!HasParam(model_params, "seasonal_period")) {
			throw InvalidInputException("SeasonalES requires 'seasonal_period' parameter");
		}
	} else if (model_name == "SeasonalESOptimized") {
		if (!HasParam(model_params, "seasonal_period")) {
			throw InvalidInputException("SeasonalESOptimized requires 'seasonal_period' parameter");
		}
	} else if (model_name == "SeasonalWindowAverage") {
		if (!HasParam(model_params, "seasonal_period")) {
			throw InvalidInputException("SeasonalWindowAverage requires 'seasonal_period' parameter");
		}
	}
	// Intermittent demand models (no parameters or simple defaults)
	else if (model_name == "CrostonClassic" || model_name == "CrostonOptimized" || model_name == "CrostonSBA" ||
	         model_name == "ADIDA" || model_name == "IMAPA") {
		// No parameters or all optional
	} else if (model_name == "TSB") {
		// alpha_d and alpha_p are optional with defaults
		if (HasParam(model_params, "alpha_d")) {
			double alpha = GetParam<double>(model_params, "alpha_d", 0.1);
			if (alpha <= 0.0 || alpha > 1.0) {
				throw InvalidInputException("TSB alpha_d must be in (0, 1]");
			}
		}
		if (HasParam(model_params, "alpha_p")) {
			double alpha = GetParam<double>(model_params, "alpha_p", 0.1);
			if (alpha <= 0.0 || alpha > 1.0) {
				throw InvalidInputException("TSB alpha_p must be in (0, 1]");
			}
		}
	} else {
		throw InvalidInputException("Unknown model: '" + model_name + "'");
	}

	// Universal parameter: method_name (optional for all models)
	if (HasParam(model_params, "method_name")) {
		std::string method_name_str = GetParam<std::string>(model_params, "method_name", "");
		if (method_name_str.empty()) {
			throw InvalidInputException("method_name parameter cannot be empty");
		}
	}
}

template <typename T>
T ModelFactory::GetParam(const Value &params, const std::string &key, const T &default_value) {
	if (params.IsNull() || params.type().id() != LogicalTypeId::STRUCT) {
		return default_value;
	}

	auto &struct_children = StructValue::GetChildren(params);
	for (idx_t i = 0; i < struct_children.size(); i++) {
		auto &child_name = StructType::GetChildName(params.type(), i);
		if (child_name == key) {
			try {
				return struct_children[i].GetValue<T>();
			} catch (const std::exception &e) {
				// std::cerr << "[DEBUG] Error extracting parameter '" << key << "': " << e.what() << std::endl;
				return default_value;
			}
		}
	}

	return default_value;
}

template <typename T>
T ModelFactory::GetRequiredParam(const Value &params, const std::string &key) {
	if (params.IsNull() || params.type().id() != LogicalTypeId::STRUCT) {
		throw InvalidInputException("Required parameter '" + key + "' not found in empty parameters");
	}

	auto &struct_children = StructValue::GetChildren(params);
	for (idx_t i = 0; i < struct_children.size(); i++) {
		auto &child_name = StructType::GetChildName(params.type(), i);
		if (child_name == key) {
			try {
				return struct_children[i].GetValue<T>();
			} catch (const std::exception &e) {
				throw InvalidInputException("Error extracting required parameter '" + key +
				                            "': " + std::string(e.what()));
			}
		}
	}

	throw InvalidInputException("Required parameter '" + key + "' not found in model parameters");
}

bool ModelFactory::HasParam(const Value &params, const std::string &key) {
	if (params.IsNull() || params.type().id() != LogicalTypeId::STRUCT) {
		return false;
	}

	auto &struct_children = StructValue::GetChildren(params);
	for (idx_t i = 0; i < struct_children.size(); i++) {
		auto &child_name = StructType::GetChildName(params.type(), i);
		if (child_name == key) {
			return true;
		}
	}

	return false;
}

std::vector<int> ModelFactory::GetArrayParam(const Value &params, const std::string &key,
                                             const std::vector<int> &default_value) {
	if (params.IsNull() || params.type().id() != LogicalTypeId::STRUCT) {
		return default_value;
	}

	auto &struct_children = StructValue::GetChildren(params);
	for (idx_t i = 0; i < struct_children.size(); i++) {
		auto &child_name = StructType::GetChildName(params.type(), i);
		if (child_name == key) {
			const auto &list_value = struct_children[i];
			if (list_value.IsNull()) {
				return default_value;
			}

			// Extract list values
			const auto &list_children = ListValue::GetChildren(list_value);
			std::vector<int> result;
			result.reserve(list_children.size());
			for (const auto &child : list_children) {
				result.push_back(child.GetValue<int>());
			}
			return result;
		}
	}

	return default_value;
}

// Explicit template instantiations
template int ModelFactory::GetParam<int>(const Value &, const std::string &, const int &);
template int ModelFactory::GetRequiredParam<int>(const Value &, const std::string &);
template double ModelFactory::GetParam<double>(const Value &, const std::string &, const double &);
template double ModelFactory::GetRequiredParam<double>(const Value &, const std::string &);

} // namespace duckdb
