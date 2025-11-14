#include "anofox-time/features/feature_types.hpp"
#include "anofox-time/features/feature_calculators.hpp"
#include "anofox-time/features/feature_math.hpp"
#include <cmath>
#include <limits>
#include <sstream>

namespace anofoxtime::features {

namespace {

std::string NormalizeDouble(double value) {
	std::ostringstream oss;
	oss.setf(std::ios::fixed, std::ios::floatfield);
	oss.precision(12);
	oss << value;
	auto repr = oss.str();
	while (repr.size() > 2 && repr.back() == '0') {
		repr.pop_back();
	}
	if (!repr.empty() && repr.back() == '.') {
		repr.pop_back();
	}
	return repr;
}

std::string VariantToString(const FeatureParamValue &value) {
	struct Visitor {
		std::string operator()(std::monostate) const {
			return "none";
		}
		std::string operator()(bool v) const {
			return v ? "true" : "false";
		}
		std::string operator()(int64_t v) const {
			return std::to_string(v);
		}
		std::string operator()(double v) const {
			return NormalizeDouble(v);
		}
		std::string operator()(const std::string &v) const {
			return v;
		}
		std::string operator()(const std::vector<double> &vec) const {
			std::ostringstream oss;
			oss << "(";
			for (size_t i = 0; i < vec.size(); ++i) {
				if (i > 0) {
					oss << "|";
				}
				oss << NormalizeDouble(vec[i]);
			}
			oss << ")";
			return oss.str();
		}
		std::string operator()(const std::vector<int64_t> &vec) const {
			std::ostringstream oss;
			oss << "(";
			for (size_t i = 0; i < vec.size(); ++i) {
				if (i > 0) {
					oss << "|";
				}
				oss << vec[i];
			}
			oss << ")";
			return oss.str();
		}
	};
	return std::visit(Visitor {}, value);
}

} // namespace

std::optional<double> ParameterMap::GetDouble(std::string_view key) const {
	auto it = entries.find(std::string(key));
	if (it == entries.end()) {
		return std::nullopt;
	}
	const auto &value = it->second;
	if (std::holds_alternative<double>(value)) {
		return std::get<double>(value);
	}
	if (std::holds_alternative<int64_t>(value)) {
		return static_cast<double>(std::get<int64_t>(value));
	}
	if (std::holds_alternative<bool>(value)) {
		return std::get<bool>(value) ? 1.0 : 0.0;
	}
	return std::nullopt;
}

std::optional<int64_t> ParameterMap::GetInt(std::string_view key) const {
	auto it = entries.find(std::string(key));
	if (it == entries.end()) {
		return std::nullopt;
	}
	const auto &value = it->second;
	if (std::holds_alternative<int64_t>(value)) {
		return std::get<int64_t>(value);
	}
	if (std::holds_alternative<double>(value)) {
		return static_cast<int64_t>(std::llround(std::get<double>(value)));
	}
	return std::nullopt;
}

std::optional<bool> ParameterMap::GetBool(std::string_view key) const {
	auto it = entries.find(std::string(key));
	if (it == entries.end()) {
		return std::nullopt;
	}
	const auto &value = it->second;
	if (std::holds_alternative<bool>(value)) {
		return std::get<bool>(value);
	}
	if (std::holds_alternative<int64_t>(value)) {
		return std::get<int64_t>(value) != 0;
	}
	if (std::holds_alternative<double>(value)) {
		return std::fabs(std::get<double>(value)) > std::numeric_limits<double>::epsilon();
	}
	return std::nullopt;
}

std::optional<std::string> ParameterMap::GetString(std::string_view key) const {
	auto it = entries.find(std::string(key));
	if (it == entries.end()) {
		return std::nullopt;
	}
	const auto &value = it->second;
	if (std::holds_alternative<std::string>(value)) {
		return std::get<std::string>(value);
	}
	return std::nullopt;
}

std::optional<std::vector<double>> ParameterMap::GetDoubleVector(std::string_view key) const {
	auto it = entries.find(std::string(key));
	if (it == entries.end()) {
		return std::nullopt;
	}
	const auto &value = it->second;
	if (std::holds_alternative<std::vector<double>>(value)) {
		return std::get<std::vector<double>>(value);
	}
	if (std::holds_alternative<std::vector<int64_t>>(value)) {
		std::vector<double> output;
		for (auto entry : std::get<std::vector<int64_t>>(value)) {
			output.push_back(static_cast<double>(entry));
		}
		return output;
	}
	return std::nullopt;
}

std::string ParameterMap::ToSuffixString() const {
	if (entries.empty()) {
		return "";
	}
	std::ostringstream oss;
	for (const auto &kv : entries) {
		oss << "__" << kv.first << "_" << VariantToString(kv.second);
	}
	return oss.str();
}

FeatureRegistry::FeatureRegistry() {
	RegisterBuiltinFeatureCalculators(*this);
	FinalizeDefaultConfig();
}

FeatureRegistry &FeatureRegistry::Instance() {
	static FeatureRegistry registry;
	return registry;
}

void FeatureRegistry::Register(FeatureDefinition def) {
	features_.push_back(std::move(def));
}

void FeatureRegistry::FinalizeDefaultConfig() {
	default_config_.requests.clear();
	for (const auto &feature : features_) {
		FeatureRequest request;
		request.name = feature.name;
		if (feature.default_parameters.empty()) {
			request.parameters.emplace_back();
		} else {
			request.parameters = feature.default_parameters;
		}
		default_config_.requests.push_back(request);
	}
}

const FeatureDefinition *FeatureRegistry::Find(std::string_view name) const {
	for (const auto &feature : features_) {
		if (feature.name == name) {
			return &feature;
		}
	}
	return nullptr;
}

std::vector<FeatureResult> FeatureRegistry::Compute(const Series &series, const FeatureConfig &config,
                                                    const std::vector<double> *time_axis) const {
	std::vector<FeatureResult> results;
	FeatureCache cache(series, time_axis);
	for (const auto &request : config.requests) {
		auto feature = Find(request.name);
		if (!feature) {
			continue;
		}
		const auto &params = request.parameters.empty() ? std::vector<ParameterMap>{ParameterMap {}} : request.parameters;
		for (const auto &param : params) {
			FeatureResult result;
			result.name = request.name + param.ToSuffixString();
			result.value = feature->calculator(series, param, cache);
			result.is_nan = std::isnan(result.value);
			results.push_back(result);
		}
	}
	return results;
}

} // namespace anofoxtime::features

