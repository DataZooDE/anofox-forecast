#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace anofoxtime::features {

using Series = std::vector<double>;

using FeatureParamValue =
    std::variant<std::monostate, bool, int64_t, double, std::string, std::vector<double>, std::vector<int64_t>>;

struct ParameterMap {
	std::map<std::string, FeatureParamValue> entries;

	bool Has(std::string_view key) const {
		return entries.find(std::string(key)) != entries.end();
	}

	void Set(std::string key, FeatureParamValue value) {
		entries.emplace(std::move(key), std::move(value));
	}

	std::optional<double> GetDouble(std::string_view key) const;
	std::optional<int64_t> GetInt(std::string_view key) const;
	std::optional<bool> GetBool(std::string_view key) const;
	std::optional<std::string> GetString(std::string_view key) const;
	std::optional<std::vector<double>> GetDoubleVector(std::string_view key) const;
	std::string ToSuffixString() const;
};

inline ParameterMap Params(std::initializer_list<std::pair<std::string, FeatureParamValue>> initializer) {
	ParameterMap param_map;
	for (const auto &entry : initializer) {
		param_map.entries.insert(entry);
	}
	return param_map;
}

struct FeatureResult {
	std::string name;
	double value;
	bool is_nan = false;
};

class FeatureCache;

using FeatureCalculatorFn = std::function<double(const Series &, const ParameterMap &, FeatureCache &)>;

struct FeatureDefinition {
	std::string name;
	std::vector<ParameterMap> default_parameters;
	FeatureCalculatorFn calculator;
	size_t default_parameter_index = 0;
};

struct FeatureRequest {
	std::string name;
	std::vector<ParameterMap> parameters;
};

struct FeatureConfig {
	std::vector<FeatureRequest> requests;
};

class FeatureRegistry {
public:
	static FeatureRegistry &Instance();

	const FeatureDefinition *Find(std::string_view name) const;
	const FeatureConfig &DefaultConfig() const {
		return default_config_;
	}
	const std::vector<FeatureDefinition> &Definitions() const {
		return features_;
	}

	std::vector<FeatureResult> Compute(const Series &series, const FeatureConfig &config,
	                                   const std::vector<double> *time_axis_hours = nullptr) const;

	void Register(FeatureDefinition def);
	void FinalizeDefaultConfig();

private:
	FeatureRegistry();
	std::vector<FeatureDefinition> features_;
	FeatureConfig default_config_;
};

} // namespace anofoxtime::features

