#pragma once

#include "duckdb.hpp"
#include "anofox_time_wrapper.hpp"
#include <memory>
#include <string>

namespace duckdb {

class ModelFactory {
public:
	// Create a forecaster instance from model name and parameters
	static std::unique_ptr<::anofoxtime::models::IForecaster> Create(const std::string &model_name,
	                                                                 const Value &model_params);

	// Get list of supported model names
	static std::vector<std::string> GetSupportedModels();

	// Validate model parameters
	static void ValidateModelParams(const std::string &model_name, const Value &model_params);

private:
	// Helper to extract parameter from STRUCT with default value
	template <typename T>
	static T GetParam(const Value &params, const std::string &key, const T &default_value);

	// Helper to extract required parameter from STRUCT
	template <typename T>
	static T GetRequiredParam(const Value &params, const std::string &key);

	// Helper to extract array parameter
	static std::vector<int> GetArrayParam(const Value &params, const std::string &key,
	                                      const std::vector<int> &default_value);

	// Check if parameter exists in STRUCT
	static bool HasParam(const Value &params, const std::string &key);
};

} // namespace duckdb
