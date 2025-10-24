#include "model_factory.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <iostream>

// Need to include full types for unique_ptr destructor
#include "anofox-time/models/iforecaster.hpp"

namespace duckdb {

std::unique_ptr<::anofoxtime::models::IForecaster> ModelFactory::Create(
    const std::string& model_name, 
    const Value& model_params
) {
    // std::cerr << "[DEBUG] ModelFactory::Create called with model: " << model_name << std::endl;
    
    // Validate parameters first
    ValidateModelParams(model_name, model_params);
    
    if (model_name == "SMA") {
        int window = GetParam<int>(model_params, "window", 5);
        // std::cerr << "[DEBUG] Creating SMA model with window: " << window << std::endl;
        return AnofoxTimeWrapper::CreateSMA(window);
    } 
    else if (model_name == "Naive") {
        // std::cerr << "[DEBUG] Creating Naive model" << std::endl;
        return AnofoxTimeWrapper::CreateNaive();
    } 
    else if (model_name == "SeasonalNaive") {
        int period = GetRequiredParam<int>(model_params, "seasonal_period");
        // std::cerr << "[DEBUG] Creating SeasonalNaive model with period: " << period << std::endl;
        return AnofoxTimeWrapper::CreateSeasonalNaive(period);
    }
    else if (model_name == "SES") {
        double alpha = GetParam<double>(model_params, "alpha", 0.3);
        // std::cerr << "[DEBUG] Creating SES model with alpha: " << alpha << std::endl;
        return AnofoxTimeWrapper::CreateSES(alpha);
    }
    else if (model_name == "Theta") {
        int seasonal_period = GetParam<int>(model_params, "seasonal_period", 1);
        double theta_param = GetParam<double>(model_params, "theta", 2.0);
        // std::cerr << "[DEBUG] Creating Theta model with period: " << seasonal_period 
        //           << ", theta: " << theta_param << std::endl;
        return AnofoxTimeWrapper::CreateTheta(seasonal_period, theta_param);
    }
    else if (model_name == "Holt") {
        double alpha = GetParam<double>(model_params, "alpha", 0.3);
        double beta = GetParam<double>(model_params, "beta", 0.1);
        // std::cerr << "[DEBUG] Creating Holt model with alpha: " << alpha 
        //           << ", beta: " << beta << std::endl;
        return AnofoxTimeWrapper::CreateHolt(alpha, beta);
    }
    else if (model_name == "HoltWinters") {
        int seasonal_period = GetRequiredParam<int>(model_params, "seasonal_period");
        bool multiplicative = GetParam<int>(model_params, "multiplicative", 0) != 0;
        double alpha = GetParam<double>(model_params, "alpha", 0.2);
        double beta = GetParam<double>(model_params, "beta", 0.1);
        double gamma = GetParam<double>(model_params, "gamma", 0.1);
        // std::cerr << "[DEBUG] Creating HoltWinters model with period: " << seasonal_period 
        //           << ", multiplicative: " << multiplicative << std::endl;
        return AnofoxTimeWrapper::CreateHoltWinters(seasonal_period, multiplicative, alpha, beta, gamma);
    }
    else if (model_name == "AutoARIMA") {
        int seasonal_period = GetParam<int>(model_params, "seasonal_period", 0);
        // std::cerr << "[DEBUG] Creating AutoARIMA model with period: " << seasonal_period << std::endl;
        return AnofoxTimeWrapper::CreateAutoARIMA(seasonal_period);
    }
    else if (model_name == "ETS") {
        int error_type = GetParam<int>(model_params, "error_type", 0);  // 0=Additive
        int trend_type = GetParam<int>(model_params, "trend_type", 0);  // 0=None
        int season_type = GetParam<int>(model_params, "season_type", 0);  // 0=None
        int season_length = GetParam<int>(model_params, "season_length", 1);
        double alpha = GetParam<double>(model_params, "alpha", 0.2);
        double beta = GetParam<double>(model_params, "beta", 0.1);
        double gamma = GetParam<double>(model_params, "gamma", 0.1);
        double phi = GetParam<double>(model_params, "phi", 0.98);
        // std::cerr << "[DEBUG] Creating ETS model with error:" << error_type 
        //           << ", trend:" << trend_type << ", season:" << season_type << std::endl;
        return AnofoxTimeWrapper::CreateETS(error_type, trend_type, season_type, season_length,
                                           alpha, beta, gamma, phi);
    }
    else if (model_name == "AutoETS") {
        int season_length = GetParam<int>(model_params, "season_length", 1);
        // std::cerr << "[DEBUG] Creating AutoETS model with season_length: " << season_length << std::endl;
        return AnofoxTimeWrapper::CreateAutoETS(season_length);
    }
    else if (model_name == "MFLES") {
        std::vector<int> seasonal_periods = GetArrayParam(model_params, "seasonal_periods", {12});
        int n_iterations = GetParam<int>(model_params, "n_iterations", 3);
        double lr_trend = GetParam<double>(model_params, "lr_trend", 0.3);
        double lr_season = GetParam<double>(model_params, "lr_season", 0.5);
        double lr_level = GetParam<double>(model_params, "lr_level", 0.8);
        // std::cerr << "[DEBUG] Creating MFLES model" << std::endl;
        return AnofoxTimeWrapper::CreateMFLES(seasonal_periods, n_iterations, lr_trend, lr_season, lr_level);
    }
    else if (model_name == "AutoMFLES") {
        std::vector<int> seasonal_periods = GetArrayParam(model_params, "seasonal_periods", {12});
        // std::cerr << "[DEBUG] Creating AutoMFLES model" << std::endl;
        return AnofoxTimeWrapper::CreateAutoMFLES(seasonal_periods);
    }
    else if (model_name == "MSTL") {
        std::vector<int> seasonal_periods = GetArrayParam(model_params, "seasonal_periods", {12});
        int trend_method = GetParam<int>(model_params, "trend_method", 0);  // 0=Linear
        int seasonal_method = GetParam<int>(model_params, "seasonal_method", 0);  // 0=Cyclic
        // std::cerr << "[DEBUG] Creating MSTL model" << std::endl;
        return AnofoxTimeWrapper::CreateMSTL(seasonal_periods, trend_method, seasonal_method);
    }
    else if (model_name == "AutoMSTL") {
        std::vector<int> seasonal_periods = GetArrayParam(model_params, "seasonal_periods", {12});
        // std::cerr << "[DEBUG] Creating AutoMSTL model" << std::endl;
        return AnofoxTimeWrapper::CreateAutoMSTL(seasonal_periods);
    }
    else {
        auto supported_models = GetSupportedModels();
        std::string supported_list;
        for (size_t i = 0; i < supported_models.size(); i++) {
            if (i > 0) supported_list += ", ";
            supported_list += supported_models[i];
        }
        throw InvalidInputException("Unknown model: '" + model_name + "'. Supported models: " + supported_list);
    }
}

std::vector<std::string> ModelFactory::GetSupportedModels() {
    return {"SMA", "Naive", "SeasonalNaive", "SES", "Theta", "Holt", "HoltWinters", "AutoARIMA", "ETS", "AutoETS", "MFLES", "AutoMFLES", "MSTL", "AutoMSTL"};
}

void ModelFactory::ValidateModelParams(const std::string& model_name, const Value& model_params) {
    // std::cerr << "[DEBUG] Validating parameters for model: " << model_name << std::endl;
    
    if (model_name == "SMA") {
        // window is optional, default to 5
        if (HasParam(model_params, "window")) {
            int window = GetParam<int>(model_params, "window", 5);
            if (window <= 0) {
                throw InvalidInputException("SMA window parameter must be positive, got: " + std::to_string(window));
            }
        }
    }
    else if (model_name == "Naive") {
        // No parameters required
    }
    else if (model_name == "SeasonalNaive") {
        // seasonal_period is required
        if (!HasParam(model_params, "seasonal_period")) {
            throw InvalidInputException("SeasonalNaive model requires 'seasonal_period' parameter");
        }
        int period = GetRequiredParam<int>(model_params, "seasonal_period");
        if (period <= 0) {
            throw InvalidInputException("SeasonalNaive seasonal_period must be positive, got: " + std::to_string(period));
        }
    }
    else if (model_name == "SES") {
        // alpha is optional with default 0.3
        if (HasParam(model_params, "alpha")) {
            double alpha = GetParam<double>(model_params, "alpha", 0.3);
            if (alpha <= 0.0 || alpha > 1.0) {
                throw InvalidInputException("SES alpha parameter must be in (0, 1], got: " + std::to_string(alpha));
            }
        }
    }
    else if (model_name == "Theta") {
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
    }
    else if (model_name == "Holt") {
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
    }
    else if (model_name == "HoltWinters") {
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
    else if (model_name == "AutoARIMA") {
        // seasonal_period is optional, defaults to 0 (non-seasonal)
        if (HasParam(model_params, "seasonal_period")) {
            int period = GetParam<int>(model_params, "seasonal_period", 0);
            if (period < 0) {
                throw InvalidInputException("AutoARIMA seasonal_period must be non-negative, got: " + std::to_string(period));
            }
        }
    }
    else if (model_name == "ETS") {
        // All parameters are optional with defaults
        if (HasParam(model_params, "error_type")) {
            int error_type = GetParam<int>(model_params, "error_type", 0);
            if (error_type < 0 || error_type > 1) {
                throw InvalidInputException("ETS error_type must be 0 (Additive) or 1 (Multiplicative), got: " + std::to_string(error_type));
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
    }
    else if (model_name == "AutoETS") {
        // season_length is optional with default 1
        if (HasParam(model_params, "season_length")) {
            int season_length = GetParam<int>(model_params, "season_length", 1);
            if (season_length < 1) {
                throw InvalidInputException("AutoETS season_length must be >= 1, got: " + std::to_string(season_length));
            }
        }
    }
    else if (model_name == "MFLES") {
        // All parameters optional with defaults
        if (HasParam(model_params, "n_iterations")) {
            int n_iter = GetParam<int>(model_params, "n_iterations", 3);
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
    }
    else if (model_name == "AutoMFLES") {
        // seasonal_periods is optional
    }
    else if (model_name == "MSTL") {
        // All parameters optional
        if (HasParam(model_params, "trend_method")) {
            int method = GetParam<int>(model_params, "trend_method", 0);
            if (method < 0 || method > 3) {
                throw InvalidInputException("MSTL trend_method must be 0-3, got: " + std::to_string(method));
            }
        }
        if (HasParam(model_params, "seasonal_method")) {
            int method = GetParam<int>(model_params, "seasonal_method", 0);
            if (method < 0 || method > 2) {
                throw InvalidInputException("MSTL seasonal_method must be 0-2, got: " + std::to_string(method));
            }
        }
    }
    else if (model_name == "AutoMSTL") {
        // seasonal_periods is optional
    }
    else {
        throw InvalidInputException("Unknown model: '" + model_name + "'");
    }
}

template<typename T>
T ModelFactory::GetParam(const Value& params, const std::string& key, const T& default_value) {
    if (params.IsNull() || params.type().id() != LogicalTypeId::STRUCT) {
        return default_value;
    }
    
    auto &struct_children = StructValue::GetChildren(params);
    for (idx_t i = 0; i < struct_children.size(); i++) {
        auto &child_name = StructType::GetChildName(params.type(), i);
        if (child_name == key) {
            try {
                return struct_children[i].GetValue<T>();
            } catch (const std::exception& e) {
                // std::cerr << "[DEBUG] Error extracting parameter '" << key << "': " << e.what() << std::endl;
                return default_value;
            }
        }
    }
    
    return default_value;
}

template<typename T>
T ModelFactory::GetRequiredParam(const Value& params, const std::string& key) {
    if (params.IsNull() || params.type().id() != LogicalTypeId::STRUCT) {
        throw InvalidInputException("Required parameter '" + key + "' not found in empty parameters");
    }
    
    auto &struct_children = StructValue::GetChildren(params);
    for (idx_t i = 0; i < struct_children.size(); i++) {
        auto &child_name = StructType::GetChildName(params.type(), i);
        if (child_name == key) {
            try {
                return struct_children[i].GetValue<T>();
            } catch (const std::exception& e) {
                throw InvalidInputException("Error extracting required parameter '" + key + "': " + std::string(e.what()));
            }
        }
    }
    
    throw InvalidInputException("Required parameter '" + key + "' not found in model parameters");
}

bool ModelFactory::HasParam(const Value& params, const std::string& key) {
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

std::vector<int> ModelFactory::GetArrayParam(const Value& params, const std::string& key, const std::vector<int>& default_value) {
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
template int ModelFactory::GetParam<int>(const Value&, const std::string&, const int&);
template int ModelFactory::GetRequiredParam<int>(const Value&, const std::string&);
template double ModelFactory::GetParam<double>(const Value&, const std::string&, const double&);
template double ModelFactory::GetRequiredParam<double>(const Value&, const std::string&);

} // namespace duckdb