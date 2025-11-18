#ifdef DUCKDB_BUILD_LIBRARY
#define DUCKDB_TEMP_BUILD_LIBRARY
#undef DUCKDB_BUILD_LIBRARY
#endif

#include "ts_features_function.hpp"
#include "anofox-time/features/feature_types.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/parser/parsed_data/create_aggregate_function_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/common/types/string_type.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/constants.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/serializer/buffered_file_reader.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/vector_size.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/main/client_data.hpp"
#include "yyjson.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cstdlib>
#include <cerrno>
#include <optional>

#ifdef DUCKDB_TEMP_BUILD_LIBRARY
#define DUCKDB_BUILD_LIBRARY
#undef DUCKDB_TEMP_BUILD_LIBRARY
#endif

namespace duckdb {

using namespace anofoxtime::features;
using namespace duckdb_yyjson;

namespace {

struct FeatureColumnSpec {
	std::string feature_name;
	ParameterMap parameters;
};

using ColumnLookup = std::unordered_map<std::string, FeatureColumnSpec>;

ColumnLookup BuildColumnLookup(const FeatureConfig &config) {
	ColumnLookup lookup;
	for (const auto &request : config.requests) {
		auto params = request.parameters.empty() ? std::vector<ParameterMap> {ParameterMap {}} : request.parameters;
		for (const auto &param : params) {
			auto column_name = request.name + param.ToSuffixString();
			FeatureColumnSpec spec;
			spec.feature_name = request.name;
			spec.parameters = param;
			lookup.emplace(column_name, spec);
		}
	}
	return lookup;
}

void BuildColumnsFromConfig(const FeatureConfig &config, std::vector<std::string> &column_names,
                            child_list_t<LogicalType> &children) {
	for (const auto &request : config.requests) {
		auto params = request.parameters.empty() ? std::vector<ParameterMap> {ParameterMap {}} : request.parameters;
		for (const auto &param : params) {
			auto column_name = request.name + param.ToSuffixString();
			column_names.push_back(column_name);
			children.emplace_back(column_name, LogicalType(LogicalTypeId::DOUBLE));
		}
	}
}

FeatureConfig BuildFilteredConfig(const FeatureConfig &default_config, const std::vector<std::string> &selected) {
	FeatureConfig filtered;
	ColumnLookup lookup = BuildColumnLookup(default_config);
	std::unordered_set<std::string> seen;
	std::unordered_map<std::string, idx_t> request_index;

	for (const auto &name : selected) {
		if (name.empty()) {
			throw InvalidInputException("ts_features feature names must be non-empty strings");
		}
		if (!seen.insert(name).second) {
			throw InvalidInputException("ts_features feature '%s' specified more than once", name);
		}
		auto entry = lookup.find(name);
		if (entry != lookup.end()) {
			FeatureRequest *request_ptr = nullptr;
			auto req_idx = request_index.find(entry->second.feature_name);
			if (req_idx == request_index.end()) {
				FeatureRequest request;
				request.name = entry->second.feature_name;
				filtered.requests.push_back(request);
				auto new_idx = filtered.requests.size() - 1;
				request_index.emplace(entry->second.feature_name, new_idx);
				request_ptr = &filtered.requests.back();
			} else {
				request_ptr = &filtered.requests[req_idx->second];
			}
			request_ptr->parameters.push_back(entry->second.parameters);
			continue;
		}
		auto definition = FeatureRegistry::Instance().Find(name);
		if (!definition) {
			throw InvalidInputException("ts_features feature '%s' does not exist. Use ts_features_list() to inspect "
			                            "available columns.",
			                            name);
		}
		FeatureRequest request;
		request.name = definition->name;
		request.parameters = definition->default_parameters.empty() ? std::vector<ParameterMap> {ParameterMap {}}
		                                                            : definition->default_parameters;
		filtered.requests.push_back(std::move(request));
	}
	return filtered;
}

static std::vector<std::string> ParseFeatureSelectionValue(const Value &constant) {
	auto value_type = constant.type();
	if (value_type.id() != LogicalTypeId::LIST || ListType::GetChildType(value_type).id() != LogicalTypeId::VARCHAR) {
		throw InvalidInputException("ts_features feature list must be of type LIST(VARCHAR)");
	}
	const auto &children = ListValue::GetChildren(constant);
	std::vector<std::string> result;
	result.reserve(children.size());
	for (const auto &child : children) {
		if (child.IsNull()) {
			throw InvalidInputException("ts_features feature list cannot contain NULL");
		}
		result.push_back(StringValue::Get(child));
	}
	return result;
}

std::vector<std::string> ExtractFeatureSelection(ClientContext &context, Expression &expr) {
	auto constant = ExpressionExecutor::EvaluateScalar(context, expr, true);
	if (constant.IsNull()) {
		return {};
	}
	return ParseFeatureSelectionValue(constant);
}

using FeatureParamOverrides = std::unordered_map<std::string, std::vector<ParameterMap>>;

static std::string DoubleToString(double value) {
	std::ostringstream ss;
	ss << std::setprecision(12) << value;
	return ss.str();
}

static std::string ParameterValueToString(const FeatureParamValue &value) {
	if (std::holds_alternative<std::monostate>(value)) {
		return "null";
	}
	if (std::holds_alternative<bool>(value)) {
		return std::get<bool>(value) ? "true" : "false";
	}
	if (std::holds_alternative<int64_t>(value)) {
		return std::to_string(std::get<int64_t>(value));
	}
	if (std::holds_alternative<double>(value)) {
		return DoubleToString(std::get<double>(value));
	}
	if (std::holds_alternative<std::string>(value)) {
		return std::get<std::string>(value);
	}
	if (std::holds_alternative<std::vector<double>>(value)) {
		std::ostringstream ss;
		ss << "[";
		const auto &vals = std::get<std::vector<double>>(value);
		for (idx_t i = 0; i < vals.size(); i++) {
			if (i > 0) {
				ss << ",";
			}
			ss << DoubleToString(vals[i]);
		}
		ss << "]";
		return ss.str();
	}
	if (std::holds_alternative<std::vector<int64_t>>(value)) {
		std::ostringstream ss;
		ss << "[";
		const auto &vals = std::get<std::vector<int64_t>>(value);
		for (idx_t i = 0; i < vals.size(); i++) {
			if (i > 0) {
				ss << ",";
			}
			ss << vals[i];
		}
		ss << "]";
		return ss.str();
	}
	throw InternalException("Unsupported parameter value type");
}

static std::string ParameterMapToString(const ParameterMap &param) {
	if (param.entries.empty()) {
		return "{}";
	}
	std::ostringstream ss;
	ss << "{";
	bool first = true;
	for (const auto &entry : param.entries) {
		if (!first) {
			ss << ", ";
		}
		first = false;
		ss << entry.first << ": " << ParameterValueToString(entry.second);
	}
	ss << "}";
	return ss.str();
}

static std::string ParameterKeysToString(const FeatureDefinition *definition) {
	if (!definition || definition->default_parameters.empty()) {
		return "";
	}
	std::set<std::string> keys;
	for (const auto &param : definition->default_parameters) {
		for (const auto &entry : param.entries) {
			keys.insert(entry.first);
		}
	}
	std::ostringstream ss;
	bool first = true;
	for (const auto &key : keys) {
		if (!first) {
			ss << ", ";
		}
		first = false;
		ss << key;
	}
	return ss.str();
}

static FeatureParamValue ParseParameterValue(const Value &value) {
	if (value.IsNull()) {
		return std::monostate();
	}
	auto type = value.type().id();
	switch (type) {
	case LogicalTypeId::BOOLEAN:
		return BooleanValue::Get(value);
	case LogicalTypeId::TINYINT:
	case LogicalTypeId::SMALLINT:
	case LogicalTypeId::INTEGER:
	case LogicalTypeId::BIGINT:
	case LogicalTypeId::UTINYINT:
	case LogicalTypeId::USMALLINT:
	case LogicalTypeId::UINTEGER:
	case LogicalTypeId::UBIGINT:
	case LogicalTypeId::HUGEINT:
	case LogicalTypeId::DECIMAL:
		return value.DefaultCastAs(LogicalType::BIGINT).GetValue<int64_t>();
	case LogicalTypeId::FLOAT:
	case LogicalTypeId::DOUBLE:
		return value.DefaultCastAs(LogicalType::DOUBLE).GetValue<double>();
	case LogicalTypeId::VARCHAR:
	case LogicalTypeId::ENUM:
		return StringValue::Get(value.DefaultCastAs(LogicalType::VARCHAR));
	case LogicalTypeId::LIST: {
		auto child_type = ListType::GetChildType(value.type());
		const auto &elements = ListValue::GetChildren(value);
		if (TypeIsIntegral(child_type.InternalType())) {
			std::vector<int64_t> values;
			values.reserve(elements.size());
			for (auto &child : elements) {
				values.push_back(child.DefaultCastAs(LogicalType::BIGINT).GetValue<int64_t>());
			}
			return values;
		}
		if (child_type.InternalType() == PhysicalType::DOUBLE || child_type.InternalType() == PhysicalType::FLOAT ||
		    child_type.InternalType() == PhysicalType::INT128) {
			std::vector<double> values;
			values.reserve(elements.size());
			for (auto &child : elements) {
				values.push_back(child.DefaultCastAs(LogicalType::DOUBLE).GetValue<double>());
			}
			return values;
		}
		throw InvalidInputException("Unsupported parameter list element type: %s", child_type.ToString());
	}
	default:
		break;
	}
	throw InvalidInputException("Unsupported parameter value type: %s", value.type().ToString());
}

static ParameterMap ParseParameterMapValue(const Value &value) {
	if (value.IsNull()) {
		return ParameterMap {};
	}
	if (value.type().id() != LogicalTypeId::STRUCT && value.type().id() != LogicalTypeId::MAP) {
		throw InvalidInputException("feature_params entries must be STRUCT values");
	}

	ParameterMap result;
	if (value.type().id() == LogicalTypeId::MAP) {
		const auto &pairs = MapValue::GetChildren(value);
		for (auto &entry : pairs) {
			const auto &kv = StructValue::GetChildren(entry);
			auto key = kv[0].DefaultCastAs(LogicalType::VARCHAR);
			auto key_name = StringValue::Get(key);
			result.entries.emplace(key_name, ParseParameterValue(kv[1]));
		}
		return result;
	}

	const auto &child_types = StructType::GetChildTypes(value.type());
	const auto &children = StructValue::GetChildren(value);
	for (idx_t i = 0; i < children.size(); i++) {
		result.entries.emplace(child_types[i].first, ParseParameterValue(children[i]));
	}
	return result;
}

static FeatureParamOverrides ParseFeatureOverrides(const Value &value) {
	FeatureParamOverrides overrides;
	if (value.IsNull()) {
		return overrides;
	}
	auto type = value.type().id();
	if (type != LogicalTypeId::LIST && type != LogicalTypeId::MAP) {
		throw InvalidInputException("feature_params must be LIST(STRUCT(feature, params)) or MAP(VARCHAR, ANY)");
	}
	if (type == LogicalTypeId::MAP) {
		const auto &entries = MapValue::GetChildren(value);
		for (auto &entry : entries) {
			const auto &kv = StructValue::GetChildren(entry);
			auto feature_name = StringValue::Get(kv[0].DefaultCastAs(LogicalType::VARCHAR));
			auto &slot = overrides[feature_name];
			const auto &param_value = kv[1];
			if (param_value.type().id() == LogicalTypeId::LIST) {
				const auto &list_children = ListValue::GetChildren(param_value);
				for (auto &child : list_children) {
					slot.push_back(ParseParameterMapValue(child));
				}
			} else {
				slot.push_back(ParseParameterMapValue(param_value));
			}
		}
		return overrides;
	}
	const auto &entries = ListValue::GetChildren(value);
	for (auto &entry : entries) {
		if (entry.type().id() != LogicalTypeId::STRUCT) {
			throw InvalidInputException("Each element in feature_params must be a STRUCT");
		}
		const auto &child_types = StructType::GetChildTypes(entry.type());
		const auto &children = StructValue::GetChildren(entry);
		std::string feature_name;
		Value params_value;
		for (idx_t i = 0; i < child_types.size(); i++) {
			auto field_name = StringUtil::Lower(child_types[i].first);
			if (field_name == "feature") {
				feature_name = StringValue::Get(children[i].DefaultCastAs(LogicalType::VARCHAR));
			} else if (field_name == "params") {
				params_value = children[i];
			}
		}
		if (feature_name.empty()) {
			throw InvalidInputException("Feature parameter overrides must include a 'feature' field");
		}
		overrides[feature_name].push_back(ParseParameterMapValue(params_value));
	}
	return overrides;
}

static FeatureParamOverrides ExtractFeatureOverrides(ClientContext &context, Expression &expr) {
	auto constant = ExpressionExecutor::EvaluateScalar(context, expr, true);
	return ParseFeatureOverrides(constant);
}

const LogicalType &TSFeaturesOverrideStructType() {
	static LogicalType override_type =
	    LogicalType::STRUCT({{"feature", LogicalType::VARCHAR}, {"params", LogicalType::VARCHAR}});
	return override_type;
}

const LogicalType &TSFeaturesConfigStructType() {
	static LogicalType override_list_type = LogicalType::LIST(TSFeaturesOverrideStructType());
	static LogicalType config_type = LogicalType::STRUCT(
	    {{"feature_names", LogicalType::LIST(LogicalType::VARCHAR)}, {"overrides", override_list_type}});
	return config_type;
}

enum class TSFeaturesConfigSource { JSON, CSV };

static std::mutex ts_features_config_cache_lock;
static std::unordered_map<std::string, std::shared_ptr<Value>> ts_features_config_json_cache;
static std::unordered_map<std::string, std::shared_ptr<Value>> ts_features_config_csv_cache;

static ParameterMap BuildParameterMap(const std::unordered_map<std::string, Value> &entries) {
	ParameterMap result;
	for (const auto &entry : entries) {
		result.entries.emplace(entry.first, ParseParameterValue(entry.second));
	}
	return result;
}

static Value BuildConfigValue(std::vector<Value> feature_columns, std::vector<Value> overrides) {
	child_list_t<Value> children;
	children.emplace_back("feature_names", Value::LIST(LogicalType::VARCHAR, std::move(feature_columns)));
	children.emplace_back("overrides", Value::LIST(TSFeaturesOverrideStructType(), std::move(overrides)));
	return Value::STRUCT(std::move(children));
}

static yyjson_mut_val *SerializeFeatureParamValueJSON(yyjson_mut_doc *doc, const FeatureParamValue &value) {
	if (std::holds_alternative<std::monostate>(value)) {
		return yyjson_mut_null(doc);
	}
	if (std::holds_alternative<bool>(value)) {
		return yyjson_mut_bool(doc, std::get<bool>(value));
	}
	if (std::holds_alternative<int64_t>(value)) {
		return yyjson_mut_sint(doc, std::get<int64_t>(value));
	}
	if (std::holds_alternative<double>(value)) {
		return yyjson_mut_real(doc, std::get<double>(value));
	}
	if (std::holds_alternative<std::string>(value)) {
		const auto &str = std::get<std::string>(value);
		return yyjson_mut_strncpy(doc, str.c_str(), str.size());
	}
	if (std::holds_alternative<std::vector<double>>(value)) {
		auto arr = yyjson_mut_arr(doc);
		for (auto entry : std::get<std::vector<double>>(value)) {
			yyjson_mut_arr_add_real(doc, arr, entry);
		}
		return arr;
	}
	if (std::holds_alternative<std::vector<int64_t>>(value)) {
		auto arr = yyjson_mut_arr(doc);
		for (auto entry : std::get<std::vector<int64_t>>(value)) {
			yyjson_mut_arr_add_sint(doc, arr, entry);
		}
		return arr;
	}
	throw InternalException("Unsupported parameter value type");
}

static std::string SerializeParameterMapToJSON(const ParameterMap &param) {
	auto doc = yyjson_mut_doc_new(nullptr);
	auto root = yyjson_mut_obj(doc);
	yyjson_mut_doc_set_root(doc, root);
	for (const auto &entry : param.entries) {
		auto value = SerializeFeatureParamValueJSON(doc, entry.second);
		auto key_val = yyjson_mut_strncpy(doc, entry.first.c_str(), entry.first.size());
		yyjson_mut_obj_add(root, key_val, value);
	}
	yyjson_write_err err;
	size_t len = 0;
	auto json_c = yyjson_mut_write_opts(doc, YYJSON_WRITE_ALLOW_INVALID_UNICODE, nullptr, &len, &err);
	if (!json_c) {
		yyjson_mut_doc_free(doc);
		throw InvalidInputException("Failed to serialize feature parameters to JSON: %s",
		                            err.msg ? err.msg : "unknown error");
	}
	std::string result(json_c, len);
	free(json_c);
	yyjson_mut_doc_free(doc);
	return result;
}

static FeatureParamValue ParseFeatureParamValueFromJSON(yyjson_val *val) {
	if (!val || yyjson_is_null(val)) {
		return std::monostate();
	}
	if (yyjson_is_bool(val)) {
		return yyjson_get_bool(val);
	}
	if (yyjson_is_sint(val)) {
		return static_cast<int64_t>(yyjson_get_sint(val));
	}
	if (yyjson_is_uint(val)) {
		return static_cast<int64_t>(yyjson_get_uint(val));
	}
	if (yyjson_is_real(val)) {
		return yyjson_get_real(val);
	}
	if (yyjson_is_str(val)) {
		auto str = yyjson_get_str(val);
		auto len = yyjson_get_len(val);
		return std::string(str ? str : "", len);
	}
	if (yyjson_is_arr(val)) {
		std::vector<double> double_values;
		std::vector<int64_t> int_values;
		bool has_double = false;
		yyjson_val *entry = nullptr;
		size_t idx = 0;
		size_t max = 0;
		yyjson_arr_foreach(val, idx, max, entry) {
			if (yyjson_is_null(entry)) {
				continue;
			}
			if (yyjson_is_real(entry)) {
				if (!has_double) {
					double_values.reserve(int_values.size());
					for (auto iv : int_values) {
						double_values.push_back(static_cast<double>(iv));
					}
					int_values.clear();
				}
				has_double = true;
				double_values.push_back(yyjson_get_real(entry));
				continue;
			}
			if (yyjson_is_sint(entry)) {
				if (has_double) {
					double_values.push_back(static_cast<double>(yyjson_get_sint(entry)));
				} else {
					int_values.push_back(static_cast<int64_t>(yyjson_get_sint(entry)));
				}
				continue;
			}
			if (yyjson_is_uint(entry)) {
				if (has_double) {
					double_values.push_back(static_cast<double>(yyjson_get_uint(entry)));
				} else {
					int_values.push_back(static_cast<int64_t>(yyjson_get_uint(entry)));
				}
				continue;
			}
			throw InvalidInputException("ts_features configuration contains unsupported JSON array element");
		}
		if (has_double) {
			return double_values;
		}
		return int_values;
	}
	throw InvalidInputException("ts_features configuration contains unsupported JSON value");
}

static ParameterMap ParseParameterMapFromJSONString(const std::string &json_text) {
	yyjson_read_err err;
	auto doc = yyjson_read_opts(const_cast<char *>(json_text.c_str()), json_text.size(),
	                            YYJSON_READ_ALLOW_INVALID_UNICODE, nullptr, &err);
	if (!doc) {
		throw InvalidInputException("Failed to parse ts_features parameter JSON: %s",
		                            err.msg ? err.msg : "unknown error");
	}
	auto root = yyjson_doc_get_root(doc);
	if (!yyjson_is_obj(root)) {
		yyjson_doc_free(doc);
		throw InvalidInputException("ts_features parameter JSON must be an object");
	}
	ParameterMap result;
	yyjson_val *key = nullptr;
	yyjson_val *val = nullptr;
	size_t idx = 0;
	size_t max = 0;
	yyjson_obj_foreach(root, idx, max, key, val) {
		auto key_str = yyjson_get_str(key);
		auto key_len = yyjson_get_len(key);
		if (!key_str) {
			continue;
		}
		std::string param_name(key_str, key_len);
		result.entries.emplace(std::move(param_name), ParseFeatureParamValueFromJSON(val));
	}
	yyjson_doc_free(doc);
	return result;
}

static std::pair<Value, std::string> BuildOverrideStruct(const std::string &feature,
                                                         const ParameterMap &parameter_map) {
	auto column_name = feature + parameter_map.ToSuffixString();
	auto params_json = SerializeParameterMapToJSON(parameter_map);
	auto params_value = Value(params_json);
	child_list_t<Value> children;
	children.emplace_back("feature", Value(feature));
	children.emplace_back("params", std::move(params_value));
	return {Value::STRUCT(std::move(children)), column_name};
}

static std::string ReadFileContents(FileSystem &fs, ClientContext &context, const std::string &path) {
	BufferedFileReader reader(fs, path.c_str(), FileLockType::READ_LOCK);
	auto file_size = reader.FileSize();
	std::string buffer;
	buffer.resize(file_size);
	if (file_size > 0) {
		reader.ReadData(reinterpret_cast<data_ptr_t>(buffer.data()), file_size);
	}
	return buffer;
}

static Value JSONScalarToValue(yyjson_val *val, const std::string &param_name) {
	if (!val || yyjson_is_null(val)) {
		return Value(LogicalType::SQLNULL);
	}
	if (yyjson_is_bool(val)) {
		return Value::BOOLEAN(yyjson_get_bool(val));
	}
	if (yyjson_is_sint(val)) {
		return Value::BIGINT(yyjson_get_sint(val));
	}
	if (yyjson_is_uint(val)) {
		return Value::UBIGINT(yyjson_get_uint(val));
	}
	if (yyjson_is_real(val)) {
		return Value::DOUBLE(yyjson_get_real(val));
	}
	if (yyjson_is_str(val)) {
		return Value(yyjson_get_str(val));
	}
	throw InvalidInputException("ts_features_config_from_json: unsupported JSON value type for parameter '%s'",
	                            param_name);
}

static Value LoadConfigFromJSONFile(const std::string &path, ClientContext &context) {
	auto &fs = FileSystem::GetFileSystem(context);
	auto contents = ReadFileContents(fs, context, path);
	yyjson_read_err error;
	auto doc = yyjson_read_opts(contents.data(), contents.size(), YYJSON_READ_ALLOW_COMMENTS, nullptr, &error);
	if (!doc) {
		throw InvalidInputException("ts_features_config_from_json('%s'): %s (position %llu)", path,
		                            error.msg ? error.msg : "unable to parse JSON",
		                            static_cast<long long unsigned>(error.pos));
	}
	auto root = yyjson_doc_get_root(doc);
	if (!yyjson_is_arr(root)) {
		yyjson_doc_free(doc);
		throw InvalidInputException("ts_features_config_from_json('%s') expects a JSON array of objects", path);
	}

	std::vector<Value> feature_values;
	std::vector<Value> overrides;
	// Track seen parameterized column names (feature + parameter suffix) to avoid duplicates
	std::unordered_set<std::string> seen_columns;

	yyjson_val *entry = nullptr;
	size_t idx = 0;
	size_t max = 0;
	yyjson_arr_foreach(root, idx, max, entry) {
		if (!yyjson_is_obj(entry)) {
			yyjson_doc_free(doc);
			throw InvalidInputException(
			    "ts_features_config_from_json('%s') expects each entry to be an object with 'feature' and 'params'",
			    path);
		}
		auto feature_val = yyjson_obj_get(entry, "feature");
		if (!feature_val || !yyjson_is_str(feature_val)) {
			yyjson_doc_free(doc);
			throw InvalidInputException(
			    "ts_features_config_from_json('%s') entry %llu is missing a string 'feature' field", path,
			    static_cast<long long unsigned>(idx));
		}
		std::string feature = yyjson_get_str(feature_val);
		std::unordered_map<std::string, Value> params_entries;
		auto params_obj = yyjson_obj_get(entry, "params");
		if (params_obj && yyjson_is_obj(params_obj)) {
			yyjson_val *key = nullptr;
			yyjson_val *val = nullptr;
			size_t pidx = 0;
			size_t pmax = 0;
			yyjson_obj_foreach(params_obj, pidx, pmax, key, val) {
				auto key_name = yyjson_get_str(key);
				if (!key_name) {
					continue;
				}
				params_entries.emplace(StringUtil::Lower(key_name), JSONScalarToValue(val, key_name));
			}
		}
		auto parameter_map = BuildParameterMap(params_entries);
		auto override_pair = BuildOverrideStruct(feature, parameter_map);
		const auto &column_name = override_pair.second; // feature + parameter suffix
		overrides.push_back(std::move(override_pair.first));
		// Include parameterized column name in feature_names, deduplicated by full column
		if (seen_columns.insert(column_name).second) {
			feature_values.emplace_back(column_name);
		}
	}
	yyjson_doc_free(doc);
	if (overrides.empty()) {
		throw InvalidInputException("ts_features_config_from_json('%s') did not produce any overrides", path);
	}
	return BuildConfigValue(std::move(feature_values), std::move(overrides));
}

static std::vector<std::string> ParseCSVLine(const std::string &line) {
	std::vector<std::string> result;
	std::string current;
	bool in_quotes = false;
	for (idx_t i = 0; i < line.size(); i++) {
		char c = line[i];
		if (c == '"') {
			if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
				current.push_back('"');
				i++;
			} else {
				in_quotes = !in_quotes;
			}
		} else if (c == ',' && !in_quotes) {
			result.push_back(current);
			current.clear();
		} else if ((c == '\r' || c == '\n') && !in_quotes) {
			continue;
		} else {
			current.push_back(c);
		}
	}
	result.push_back(current);
	return result;
}

static bool TryParseInteger(const std::string &input, int64_t &result) {
	if (input.empty()) {
		return false;
	}
	char *end = nullptr;
	errno = 0;
	auto value = std::strtoll(input.c_str(), &end, 10);
	if (errno != 0 || end != input.c_str() + input.size()) {
		return false;
	}
	result = value;
	return true;
}

static bool TryParseDouble(const std::string &input, double &result) {
	if (input.empty()) {
		return false;
	}
	char *end = nullptr;
	errno = 0;
	auto value = std::strtod(input.c_str(), &end);
	if (errno != 0 || end != input.c_str() + input.size()) {
		return false;
	}
	result = value;
	return true;
}

static bool TryParseBoolean(const std::string &input, bool &result) {
	auto lower = StringUtil::Lower(input);
	if (lower == "true" || lower == "t" || lower == "yes" || lower == "y") {
		result = true;
		return true;
	}
	if (lower == "false" || lower == "f" || lower == "no" || lower == "n") {
		result = false;
		return true;
	}
	return false;
}

static std::optional<Value> ConvertCSVScalar(const std::string &input) {
	auto trimmed = input;
	StringUtil::Trim(trimmed);
	if (trimmed.empty()) {
		return std::optional<Value>();
	}
	auto lower = StringUtil::Lower(trimmed);
	if (lower == "null") {
		return std::optional<Value>();
	}
	bool bool_val = false;
	if (TryParseBoolean(trimmed, bool_val)) {
		return Value::BOOLEAN(bool_val);
	}
	int64_t int_val = 0;
	if (TryParseInteger(trimmed, int_val)) {
		return Value::BIGINT(int_val);
	}
	double double_val = 0;
	if (TryParseDouble(trimmed, double_val)) {
		return Value::DOUBLE(double_val);
	}
	return Value(trimmed);
}

static Value LoadConfigFromCSVFile(const std::string &path, ClientContext &context) {
	auto &fs = FileSystem::GetFileSystem(context);
	auto contents = ReadFileContents(fs, context, path);
	std::stringstream stream(contents);
	std::string line;
	if (!std::getline(stream, line)) {
		throw InvalidInputException("ts_features_config_from_csv('%s') is empty", path);
	}
	StringUtil::RTrim(line);
	auto headers = ParseCSVLine(line);
	if (headers.empty()) {
		throw InvalidInputException("ts_features_config_from_csv('%s') header row is empty", path);
	}
	std::vector<std::string> normalized_headers;
	std::vector<std::string> parameter_keys;
	normalized_headers.reserve(headers.size());
	parameter_keys.reserve(headers.size());
	idx_t feature_idx = DConstants::INVALID_INDEX;
	for (idx_t i = 0; i < headers.size(); i++) {
		auto trimmed = headers[i];
		StringUtil::Trim(trimmed);
		auto lower = StringUtil::Lower(trimmed);
		normalized_headers.push_back(lower);
		std::string param_name = lower;
		bool has_param_prefix = StringUtil::StartsWith(param_name, "param_");
		if (has_param_prefix) {
			param_name = param_name.substr(6);
		}
		parameter_keys.push_back(param_name);
		if (!has_param_prefix && param_name == "feature") {
			feature_idx = i;
		}
	}
	if (feature_idx == DConstants::INVALID_INDEX) {
		throw InvalidInputException(
		    "ts_features_config_from_csv('%s') header must contain a 'feature' column (case-insensitive)", path);
	}

	std::vector<Value> feature_values;
	std::vector<Value> overrides;
	std::unordered_set<std::string> seen_features;

	while (std::getline(stream, line)) {
		StringUtil::RTrim(line);
		auto trimmed_line = line;
		StringUtil::Trim(trimmed_line);
		if (trimmed_line.empty()) {
			continue;
		}
		auto fields = ParseCSVLine(line);
		if (fields.size() < headers.size()) {
			fields.resize(headers.size());
		}
		auto feature_name = fields[feature_idx];
		StringUtil::Trim(feature_name);
		if (feature_name.empty()) {
			throw InvalidInputException(
			    "ts_features_config_from_csv('%s') encountered a row without a value in the 'feature' column", path);
		}
		std::unordered_map<std::string, Value> params_entries;
		for (idx_t col = 0; col < headers.size(); col++) {
			if (col == feature_idx) {
				continue;
			}
			auto header_lower = normalized_headers[col];
			if (header_lower.empty() || header_lower == "variant") {
				continue;
			}
			auto param_key = parameter_keys[col];
			if (param_key.empty()) {
				continue;
			}
			auto maybe_value = ConvertCSVScalar(col < fields.size() ? fields[col] : std::string());
			if (!maybe_value.has_value()) {
				continue;
			}
			params_entries.emplace(param_key, maybe_value.value());
		}
		auto parameter_map = BuildParameterMap(params_entries);
		auto override_pair = BuildOverrideStruct(feature_name, parameter_map);
		overrides.push_back(std::move(override_pair.first));
		if (seen_features.insert(feature_name).second) {
			feature_values.emplace_back(feature_name);
		}
	}

	if (overrides.empty()) {
		throw InvalidInputException("ts_features_config_from_csv('%s') did not produce any overrides", path);
	}
	return BuildConfigValue(std::move(feature_values), std::move(overrides));
}

static Value LoadConfigWithCache(TSFeaturesConfigSource source, const std::string &path, ClientContext &context) {
	std::shared_ptr<Value> cached_entry;
	{
		std::lock_guard<std::mutex> lock(ts_features_config_cache_lock);
		auto &cache =
		    source == TSFeaturesConfigSource::JSON ? ts_features_config_json_cache : ts_features_config_csv_cache;
		auto it = cache.find(path);
		if (it != cache.end()) {
			cached_entry = it->second;
		}
	}
	if (!cached_entry) {
		Value parsed = source == TSFeaturesConfigSource::JSON ? LoadConfigFromJSONFile(path, context)
		                                                      : LoadConfigFromCSVFile(path, context);
		cached_entry = std::make_shared<Value>(std::move(parsed));
		std::lock_guard<std::mutex> lock(ts_features_config_cache_lock);
		auto &cache =
		    source == TSFeaturesConfigSource::JSON ? ts_features_config_json_cache : ts_features_config_csv_cache;
		cache[path] = cached_entry;
	}
	return *cached_entry;
}

struct ParsedConfigStructValue {
	std::vector<std::string> feature_names;
	FeatureParamOverrides overrides;
};

static bool IsTSFeaturesConfigStruct(const LogicalType &type) {
	if (type.id() != LogicalTypeId::STRUCT) {
		return false;
	}
	bool has_feature_names = false;
	bool has_overrides = false;
	for (const auto &child : StructType::GetChildTypes(type)) {
		auto name = StringUtil::Lower(child.first);
		if (name == "feature_names") {
			has_feature_names = child.second.id() == LogicalTypeId::LIST &&
			                    ListType::GetChildType(child.second).id() == LogicalTypeId::VARCHAR;
		} else if (name == "overrides") {
			if (child.second.id() == LogicalTypeId::LIST &&
			    ListType::GetChildType(child.second) == TSFeaturesOverrideStructType()) {
				has_overrides = true;
			}
		}
	}
	return has_feature_names && has_overrides;
}

static ParsedConfigStructValue ParseConfigStructLiteral(const Value &value) {
	if (value.IsNull()) {
		throw InvalidInputException("ts_features config struct cannot be NULL");
	}
	if (!IsTSFeaturesConfigStruct(value.type())) {
		throw InvalidInputException("ts_features config struct must contain 'feature_names' and 'overrides' fields");
	}
	const Value *feature_list_value = nullptr;
	const Value *override_list_value = nullptr;
	const auto &child_types = StructType::GetChildTypes(value.type());
	const auto &children = StructValue::GetChildren(value);
	for (idx_t i = 0; i < children.size(); i++) {
		auto name = StringUtil::Lower(child_types[i].first);
		if (name == "feature_names") {
			feature_list_value = &children[i];
		} else if (name == "overrides") {
			override_list_value = &children[i];
		}
	}
	if (!feature_list_value) {
		throw InvalidInputException("ts_features config struct missing 'feature_names' field");
	}
	ParsedConfigStructValue result;
	if (!feature_list_value->IsNull()) {
		result.feature_names = ParseFeatureSelectionValue(*feature_list_value);
	}
	if (override_list_value && !override_list_value->IsNull()) {
		bool parsed_json_overrides = false;
		if (override_list_value->type().id() == LogicalTypeId::LIST) {
			const auto &entries = ListValue::GetChildren(*override_list_value);
			if (!entries.empty()) {
				auto entry_type = entries.front().type();
				if (entry_type.id() == LogicalTypeId::STRUCT) {
					const auto &child_types = StructType::GetChildTypes(entry_type);
					const LogicalType *params_type = nullptr;
					for (const auto &child : child_types) {
						if (StringUtil::Lower(child.first) == "params") {
							params_type = &child.second;
							break;
						}
					}
					if (params_type && params_type->id() == LogicalTypeId::VARCHAR) {
						FeatureParamOverrides overrides_map;
						for (const auto &override_entry : entries) {
							if (override_entry.type().id() != LogicalTypeId::STRUCT) {
								continue;
							}
							const auto &override_children = StructValue::GetChildren(override_entry);
							const auto &override_child_types = StructType::GetChildTypes(override_entry.type());
							std::string feature_name;
							std::string params_json;
							for (idx_t i = 0; i < override_children.size(); i++) {
								auto field_name = StringUtil::Lower(override_child_types[i].first);
								if (field_name == "feature") {
									feature_name =
									    StringValue::Get(override_children[i].DefaultCastAs(LogicalType::VARCHAR));
								} else if (field_name == "params") {
									params_json =
									    StringValue::Get(override_children[i].DefaultCastAs(LogicalType::VARCHAR));
								}
							}
							if (feature_name.empty()) {
								throw InvalidInputException(
								    "ts_features config overrides must include a 'feature' field");
							}
							auto param_map = ParseParameterMapFromJSONString(params_json);
							overrides_map[feature_name].push_back(std::move(param_map));
						}
						result.overrides = std::move(overrides_map);
						parsed_json_overrides = true;
					}
				}
			}
		}
		if (!parsed_json_overrides) {
			result.overrides = ParseFeatureOverrides(*override_list_value);
		}
	}
	return result;
}

static bool TryExtractConfigStruct(ClientContext &context, Expression &expr,
                                   std::vector<std::string> &selected_features, FeatureParamOverrides &overrides) {
	if (!IsTSFeaturesConfigStruct(expr.return_type)) {
		return false;
	}
	auto constant = ExpressionExecutor::EvaluateScalar(context, expr, true);
	auto parsed = ParseConfigStructLiteral(constant);
	selected_features = std::move(parsed.feature_names);
	overrides = std::move(parsed.overrides);
	return true;
}

static void ApplyOverrides(FeatureConfig &config, const FeatureParamOverrides &overrides) {
	if (overrides.empty()) {
		return;
	}
	std::unordered_set<std::string> matched;
	for (auto &request : config.requests) {
		auto it = overrides.find(request.name);
		if (it == overrides.end()) {
			continue;
		}
		if (it->second.empty()) {
			throw InvalidInputException("feature_params for '%s' must contain at least one parameter set",
			                            request.name);
		}
		request.parameters = it->second;
		matched.insert(request.name);
	}
	for (const auto &entry : overrides) {
		if (!matched.count(entry.first)) {
			throw InvalidInputException("feature_params references unknown or filtered feature '%s'", entry.first);
		}
	}
}

static void ExecuteTSFeaturesConfigFunction(TSFeaturesConfigSource source, DataChunk &input, ExpressionState &state,
                                            Vector &result) {
	auto count = input.size();
	auto &context = state.GetContext();

	UnifiedVectorFormat format;
	input.data[0].ToUnifiedFormat(count, format);
	auto data = UnifiedVectorFormat::GetData<string_t>(format);

	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t i = 0; i < count; i++) {
		auto idx = format.sel->get_index(i);
		if (!format.validity.RowIsValid(idx)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}
		auto path = data[idx].GetString();
		auto config_value = LoadConfigWithCache(source, path, context);
		result.SetValue(i, config_value);
	}
}

static void TSFeaturesConfigFromJSONFunction(DataChunk &input, ExpressionState &state, Vector &result) {
	ExecuteTSFeaturesConfigFunction(TSFeaturesConfigSource::JSON, input, state, result);
}

static void TSFeaturesConfigFromCSVFunction(DataChunk &input, ExpressionState &state, Vector &result) {
	ExecuteTSFeaturesConfigFunction(TSFeaturesConfigSource::CSV, input, state, result);
}

static ScalarFunction CreateTSFeaturesConfigFunction(const std::string &name, TSFeaturesConfigSource source) {
	auto function = ScalarFunction(name, {LogicalType::VARCHAR}, TSFeaturesConfigStructType(),
	                               source == TSFeaturesConfigSource::JSON ? TSFeaturesConfigFromJSONFunction
	                                                                      : TSFeaturesConfigFromCSVFunction);
	function.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
	return function;
}

struct TSFeaturesConfigTemplateState : public GlobalTableFunctionState {
	std::vector<std::pair<std::string, std::string>> rows;
	idx_t offset = 0;
};

static unique_ptr<FunctionData> TSFeaturesConfigTemplateBind(ClientContext &, TableFunctionBindInput &,
                                                             vector<LogicalType> &return_types, vector<string> &names) {
	return_types = {LogicalType::VARCHAR, LogicalType::VARCHAR};
	names = {"feature", "params_json"};
	return nullptr;
}

static unique_ptr<GlobalTableFunctionState> TSFeaturesConfigTemplateInit(ClientContext &, TableFunctionInitInput &) {
	auto state = make_uniq<TSFeaturesConfigTemplateState>();
	auto &registry = FeatureRegistry::Instance();
	const auto &config = registry.DefaultConfig();
	for (const auto &request : config.requests) {
		ParameterMap params;
		if (!request.parameters.empty()) {
			params = request.parameters.front();
		}
		auto json = SerializeParameterMapToJSON(params);
		state->rows.emplace_back(request.name, std::move(json));
	}
	return state;
}

static void TSFeaturesConfigTemplateFunction(ClientContext &, TableFunctionInput &data, DataChunk &output) {
	auto &state = data.global_state->Cast<TSFeaturesConfigTemplateState>();
	if (state.offset >= state.rows.size()) {
		output.SetCardinality(0);
		return;
	}
	idx_t count = std::min<idx_t>(STANDARD_VECTOR_SIZE, state.rows.size() - state.offset);
	auto feature_vec = FlatVector::GetData<string_t>(output.data[0]);
	auto params_vec = FlatVector::GetData<string_t>(output.data[1]);
	for (idx_t i = 0; i < count; i++) {
		const auto &row = state.rows[state.offset + i];
		feature_vec[i] = StringVector::AddString(output.data[0], row.first);
		params_vec[i] = StringVector::AddString(output.data[1], row.second);
	}
	state.offset += count;
	output.SetCardinality(count);
}

static TableFunction CreateTSFeaturesConfigTemplateFunction() {
	TableFunction function("ts_features_config_template", {}, TSFeaturesConfigTemplateFunction);
	function.bind = TSFeaturesConfigTemplateBind;
	function.init_global = TSFeaturesConfigTemplateInit;
	return function;
}

} // namespace

struct TSFeaturesSample {
	int64_t key;
	double value;
	idx_t ordinal;
};

struct TSFeaturesData {
	std::vector<TSFeaturesSample> samples;
};

// Cached results structure - heap-allocated to avoid issues with vector in state structure
struct TSFeaturesCachedResults {
	child_list_t<Value> results;
	size_t sample_count;

	TSFeaturesCachedResults() : sample_count(0) {
	}
};

struct TSFeaturesAggregateState {
	uint64_t magic; // Magic number for corruption detection
	TSFeaturesData *data;
	// Cached computed results - stored as pointer to avoid vector copy issues in window functions
	// This ensures that when Finalize is called multiple times with different states,
	// the results from the complete state are available
	TSFeaturesCachedResults *cached_results;

	// Default constructor - explicitly initialize all members
	// For LEGACY destructor aggregates, DuckDB uses placement new and calls Initialize()
	// We should NOT define copy constructor or assignment operator - DuckDB uses Combine() instead
	TSFeaturesAggregateState() : magic(0), data(nullptr), cached_results(nullptr) {
	}
};

static constexpr uint64_t TSFEATURES_MAGIC = 0xCAFEBABEDEADBEEFULL;

struct TSFeaturesBindData : public FunctionData {
	LogicalTypeId timestamp_type;
	FeatureConfig config;
	std::vector<std::string> column_names;
	LogicalType return_type;

	unique_ptr<FunctionData> Copy() const override {
		auto copy = make_uniq<TSFeaturesBindData>();
		copy->timestamp_type = timestamp_type;
		copy->config = config;
		copy->column_names = column_names;
		copy->return_type = return_type;
		return copy;
	}

	bool Equals(const FunctionData &other_p) const override {
		auto &other = other_p.Cast<TSFeaturesBindData>();
		return other.timestamp_type == timestamp_type && other.column_names == column_names;
	}
};

static int64_t ConvertTimestampToKey(const UnifiedVectorFormat &format, idx_t idx, LogicalTypeId type) {
	switch (type) {
	case LogicalTypeId::TIMESTAMP: {
		auto data = UnifiedVectorFormat::GetData<timestamp_t>(format);
		return data[idx].value;
	}
	case LogicalTypeId::DATE: {
		auto data = UnifiedVectorFormat::GetData<date_t>(format);
		return static_cast<int64_t>(data[idx].days) * 86400000000LL;
	}
	case LogicalTypeId::BIGINT: {
		auto data = UnifiedVectorFormat::GetData<int64_t>(format);
		return data[idx];
	}
	default: {
		auto data = UnifiedVectorFormat::GetData<int64_t>(format);
		return data[idx];
	}
	}
}

static void TSFeaturesUpdate(Vector inputs[], AggregateInputData &aggr_input, idx_t input_count, Vector &state_vector,
                             idx_t count) {
	if (count == 0) {
		return;
	}

	auto &bind_data = aggr_input.bind_data->Cast<TSFeaturesBindData>();

	UnifiedVectorFormat ts_data;
	UnifiedVectorFormat value_data;
	inputs[0].ToUnifiedFormat(count, ts_data);
	inputs[1].ToUnifiedFormat(count, value_data);

	state_vector.Flatten(count);
	auto states = FlatVector::GetData<TSFeaturesAggregateState *>(state_vector);

	for (idx_t i = 0; i < count; i++) {
		const auto ts_idx = ts_data.sel->get_index(i);
		const auto val_idx = value_data.sel->get_index(i);

		if (!ts_data.validity.RowIsValid(ts_idx) || !value_data.validity.RowIsValid(val_idx)) {
			continue;
		}

		auto *st_ptr = states[i];
		D_ASSERT(st_ptr); // Good to keep in debug builds

		auto &state = *st_ptr;

		// Check magic number to detect memory corruption
		D_ASSERT(state.magic == TSFEATURES_MAGIC);

		if (!state.data) {
			state.data = new TSFeaturesData();
		}
		if (!state.data) {
			continue;
		}

		int64_t key = ConvertTimestampToKey(ts_data, ts_idx, bind_data.timestamp_type);
		auto values = UnifiedVectorFormat::GetData<double>(value_data);
		double value = values[val_idx];
		state.data->samples.push_back(TSFeaturesSample {key, value, state.data->samples.size()});
	}
}

static std::vector<double> BuildTimeAxis(const std::vector<TSFeaturesSample> &samples, LogicalTypeId type) {
	std::vector<double> axis;
	axis.reserve(samples.size());
	if (samples.empty()) {
		return axis;
	}
	double base = static_cast<double>(samples.front().key);
	double scale = 1.0;
	if (type == LogicalTypeId::TIMESTAMP || type == LogicalTypeId::DATE) {
		scale = 1.0 / 3600000000.0; // microseconds to hours
	}
	for (const auto &sample : samples) {
		axis.push_back((static_cast<double>(sample.key) - base) * scale);
	}
	return axis;
}

struct TSFeaturesAggregateOperation {
	template <class STATE>
	static void Initialize(STATE &state) {
		// Set magic number first for corruption detection
		state.magic = TSFEATURES_MAGIC;
		state.data = nullptr;
		state.cached_results = nullptr;
	}

	template <class STATE, class OP>
	static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
		// Check magic numbers to detect memory corruption
		D_ASSERT(source.magic == TSFEATURES_MAGIC);
		D_ASSERT(target.magic == TSFEATURES_MAGIC);

		if (!source.data) {
			// Source has no data - ensure target is at least initialized
			if (!target.data) {
				target.data = new TSFeaturesData();
			}
			return;
		}

		// Safety check: if target has no data, initialize it and copy source's data
		if (!target.data) {
			target.data = new TSFeaturesData();
			// Deep copy samples from source to target
			target.data->samples = source.data->samples;
			// Clear cached results - they will be recomputed in Finalize
			if (target.cached_results) {
				target.cached_results->results.clear();
				target.cached_results->sample_count = 0;
			}
			return;
		}

		// Both have data - combine samples from source into target
		// DO NOT delete source.data here - let Destroy() clean up everything at the end
		target.data->samples.insert(target.data->samples.end(), source.data->samples.begin(),
		                            source.data->samples.end());
		// Clear cached results when combining states - they will be recomputed in Finalize
		if (target.cached_results) {
			target.cached_results->results.clear();
			target.cached_results->sample_count = 0;
		}
	}

	template <class STATE>
	static void Finalize(STATE &state, AggregateFinalizeData &finalize_data) {
		// Check magic number to detect memory corruption
		D_ASSERT(state.magic == TSFEATURES_MAGIC);

		auto &bind_data = finalize_data.input.bind_data->Cast<TSFeaturesBindData>();

		child_list_t<Value> children;
		children.reserve(bind_data.column_names.size());

		// Safety check: ensure state.data is initialized (should be, but defensive for window functions)
		if (!state.data) {
			// State not initialized - return NULL for all features
			for (const auto &name : bind_data.column_names) {
				children.emplace_back(name, Value());
			}
			finalize_data.result.SetValue(finalize_data.result_idx, Value::STRUCT(std::move(children)));
			return;
		}

		if (state.data->samples.empty()) {
			for (const auto &name : bind_data.column_names) {
				children.emplace_back(name, Value());
			}
			finalize_data.result.SetValue(finalize_data.result_idx, Value::STRUCT(std::move(children)));
			return;
		}

		auto &samples = state.data->samples;

		// Ensure cached_results is initialized
		if (!state.cached_results) {
			state.cached_results = new TSFeaturesCachedResults();
		}

		// Check if we have cached results from a previous Finalize call with sufficient samples
		// This handles the case where DuckDB calls Finalize multiple times
		if (!state.cached_results->results.empty() && state.cached_results->sample_count >= samples.size()) {
			child_list_t<Value> cached_copy = state.cached_results->results;
			finalize_data.result.SetValue(finalize_data.result_idx, Value::STRUCT(std::move(cached_copy)));
			return;
		}

		// Ensure we have at least 2 samples before computing
		if (samples.size() < 2) {
			// Too few samples - return NULL for all features
			for (const auto &name : bind_data.column_names) {
				children.emplace_back(name, Value());
			}
			finalize_data.result.SetValue(finalize_data.result_idx, Value::STRUCT(std::move(children)));
			return;
		}

		// Sort samples by timestamp (key) and ordinal to ensure consistent ordering
		std::stable_sort(samples.begin(), samples.end(), [](const TSFeaturesSample &a, const TSFeaturesSample &b) {
			if (a.key == b.key) {
				return a.ordinal < b.ordinal;
			}
			return a.key < b.key;
		});

		std::vector<double> series;
		series.reserve(samples.size());
		for (const auto &sample : samples) {
			series.push_back(sample.value);
		}

		auto time_axis = BuildTimeAxis(samples, bind_data.timestamp_type);
		const std::vector<double> *time_axis_ptr = time_axis.empty() ? nullptr : &time_axis;

		// Compute results
		auto &registry = FeatureRegistry::Instance();
		auto results = registry.Compute(series, bind_data.config, time_axis_ptr);

		for (idx_t idx = 0; idx < bind_data.column_names.size(); ++idx) {
			Value value;
			if (idx < results.size() && !results[idx].is_nan && std::isfinite(results[idx].value)) {
				value = Value::DOUBLE(results[idx].value);
			} else {
				value = Value();
			}
			children.emplace_back(bind_data.column_names[idx], std::move(value));
		}

		// Cache results in state for subsequent Finalize calls
		if (!state.cached_results) {
			state.cached_results = new TSFeaturesCachedResults();
		}
		state.cached_results->results = children;
		state.cached_results->sample_count = samples.size();

		finalize_data.result.SetValue(finalize_data.result_idx, Value::STRUCT(std::move(children)));
	}

	template <class STATE>
	static void Destroy(STATE &state, AggregateInputData &) {
		// Check magic number to detect memory corruption
		D_ASSERT(state.magic == TSFEATURES_MAGIC);

		// Safety check: ensure we only delete once
		if (state.data) {
			delete state.data;
			state.data = nullptr;
		}
		// Delete cached results if it exists
		if (state.cached_results) {
			delete state.cached_results;
			state.cached_results = nullptr;
		}
		// Clear magic number to help detect use-after-free
		state.magic = 0;
	}
};

static unique_ptr<FunctionData> TSFeaturesBind(ClientContext &context, AggregateFunction &function,
                                               vector<unique_ptr<Expression>> &arguments) {
	if (arguments.size() < 2 || arguments.size() > 4) {
		throw InvalidInputException(
		    "ts_features expects 2 to 4 arguments (timestamp, value, [feature_list], [feature_params])");
	}

	auto timestamp_type_id = arguments[0]->return_type.id();
	if (timestamp_type_id != LogicalTypeId::TIMESTAMP && timestamp_type_id != LogicalTypeId::DATE &&
	    timestamp_type_id != LogicalTypeId::BIGINT) {
		throw InvalidInputException("ts_features timestamp argument must be TIMESTAMP, DATE, or BIGINT");
	}

	auto bind_data = make_uniq<TSFeaturesBindData>();
	bind_data->timestamp_type = timestamp_type_id;
	auto default_config = FeatureRegistry::Instance().DefaultConfig();

	std::vector<std::string> selected_features;
	FeatureParamOverrides overrides;

	idx_t optional_idx = 2;
	bool config_struct_used = false;
	if (arguments.size() > optional_idx) {
		if (TryExtractConfigStruct(context, *arguments[optional_idx], selected_features, overrides)) {
			config_struct_used = true;
			optional_idx++;
		}
	}

	if (!config_struct_used && arguments.size() > optional_idx) {
		auto &maybe_list = arguments[optional_idx];
		bool treated_as_list = false;
		bool is_null = false;

		if (maybe_list->return_type.id() == LogicalTypeId::LIST &&
		    ListType::GetChildType(maybe_list->return_type).id() == LogicalTypeId::VARCHAR) {
			selected_features = ExtractFeatureSelection(context, *maybe_list);
			treated_as_list = true;
		} else if (maybe_list->type == ExpressionType::VALUE_CONSTANT) {
			auto constant = ExpressionExecutor::EvaluateScalar(context, *maybe_list, true);
			if (constant.IsNull()) {
				is_null = true;
			} else if (constant.type().id() == LogicalTypeId::LIST &&
			           ListType::GetChildType(constant.type()).id() == LogicalTypeId::VARCHAR) {
				selected_features = ExtractFeatureSelection(context, *maybe_list);
				treated_as_list = true;
			}
		}

		if (treated_as_list || is_null) {
			optional_idx++;
		}
	}

	if (!config_struct_used && arguments.size() > optional_idx) {
		overrides = ExtractFeatureOverrides(context, *arguments[optional_idx]);
		optional_idx++;
	}

	if (config_struct_used && optional_idx != arguments.size()) {
		throw InvalidInputException(
		    "ts_features config struct argument cannot be combined with additional feature arguments");
	}

	if (optional_idx != arguments.size()) {
		throw InvalidInputException("ts_features received arguments with unsupported types");
	}

	child_list_t<LogicalType> children;
	if (selected_features.empty()) {
		bind_data->config = default_config;
	} else {
		bind_data->config = BuildFilteredConfig(default_config, selected_features);
	}
	ApplyOverrides(bind_data->config, overrides);
	bind_data->column_names.clear();
	BuildColumnsFromConfig(bind_data->config, bind_data->column_names, children);

	bind_data->return_type = LogicalType::STRUCT(std::move(children));
	function.return_type = bind_data->return_type;

	return bind_data;
}

AggregateFunction CreateTSFeaturesFunction(const LogicalType &timestamp_type) {
	using STATE = TSFeaturesAggregateState;
	using OP = TSFeaturesAggregateOperation;

	std::vector<LogicalType> arguments;
	arguments.push_back(timestamp_type);
	arguments.emplace_back(LogicalTypeId::DOUBLE);

	AggregateFunction function(
	    "ts_features", std::move(arguments), LogicalType::STRUCT({}), AggregateFunction::StateSize<STATE>,
	    AggregateFunction::StateInitialize<STATE, OP, AggregateDestructorType::LEGACY>, TSFeaturesUpdate,
	    AggregateFunction::StateCombine<STATE, OP>, AggregateFunction::StateVoidFinalize<STATE, OP>, nullptr,
	    TSFeaturesBind, AggregateFunction::StateDestroy<STATE, OP>);
	function.order_dependent = AggregateOrderDependent::ORDER_DEPENDENT;
	function.varargs = LogicalType::ANY;
	return function;
}

static void RegisterAggregateFunctionIgnore(ExtensionLoader &loader, AggregateFunction function) {
	AggregateFunctionSet set(function.name);
	set.AddFunction(std::move(function));
	CreateAggregateFunctionInfo info(std::move(set));
	info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(info));
}

static void RegisterScalarFunctionIgnore(ExtensionLoader &loader, ScalarFunction function) {
	ScalarFunctionSet set(function.name);
	set.AddFunction(std::move(function));
	CreateScalarFunctionInfo info(std::move(set));
	info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(info));
}

static void RegisterTableFunctionIgnore(ExtensionLoader &loader, TableFunction function) {
	TableFunctionSet set(function.name);
	set.AddFunction(std::move(function));
	CreateTableFunctionInfo info(std::move(set));
	info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(info));
}

struct TSFeaturesListRow {
	std::string feature_name;
	std::string column_name;
	std::string parameter_suffix;
	std::string default_parameters;
	std::string parameter_keys;
};

struct TSFeaturesListGlobalState : public GlobalTableFunctionState {
	std::vector<TSFeaturesListRow> rows;
	idx_t offset = 0;
};

static unique_ptr<FunctionData> TSFeaturesListBind(ClientContext &, TableFunctionBindInput &,
                                                   vector<LogicalType> &return_types, vector<string> &names) {
	return_types = {LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR),
	                LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR),
	                LogicalType(LogicalTypeId::VARCHAR)};
	names = {"column_name", "feature_name", "parameter_suffix", "default_parameters", "parameter_keys"};
	return nullptr;
}

static unique_ptr<GlobalTableFunctionState> TSFeaturesListInit(ClientContext &, TableFunctionInitInput &) {
	auto state = make_uniq<TSFeaturesListGlobalState>();
	auto &registry = FeatureRegistry::Instance();
	const auto &config = registry.DefaultConfig();
	for (const auto &request : config.requests) {
		auto params = request.parameters.empty() ? std::vector<ParameterMap> {ParameterMap {}} : request.parameters;
		auto def = registry.Find(request.name);
		for (const auto &param : params) {
			TSFeaturesListRow row;
			row.feature_name = request.name;
			row.parameter_suffix = param.ToSuffixString();
			row.column_name = row.feature_name + row.parameter_suffix;
			row.default_parameters = ParameterMapToString(param);
			row.parameter_keys = ParameterKeysToString(def);
			state->rows.push_back(std::move(row));
		}
	}
	return state;
}

static void TSFeaturesListFunction(ClientContext &, TableFunctionInput &data, DataChunk &output) {
	auto &state = data.global_state->Cast<TSFeaturesListGlobalState>();
	if (state.offset >= state.rows.size()) {
		output.SetCardinality(0);
		return;
	}
	idx_t count = std::min<idx_t>(STANDARD_VECTOR_SIZE, state.rows.size() - state.offset);

	auto col_column_name = FlatVector::GetData<string_t>(output.data[0]);
	auto col_feature_name = FlatVector::GetData<string_t>(output.data[1]);
	auto col_suffix = FlatVector::GetData<string_t>(output.data[2]);
	auto col_defaults = FlatVector::GetData<string_t>(output.data[3]);
	auto col_keys = FlatVector::GetData<string_t>(output.data[4]);

	for (idx_t i = 0; i < count; ++i) {
		const auto &row = state.rows[state.offset + i];
		col_column_name[i] = StringVector::AddString(output.data[0], row.column_name);
		col_feature_name[i] = StringVector::AddString(output.data[1], row.feature_name);
		col_suffix[i] = StringVector::AddString(output.data[2], row.parameter_suffix);
		col_defaults[i] = StringVector::AddString(output.data[3], row.default_parameters);
		col_keys[i] = StringVector::AddString(output.data[4], row.parameter_keys);
	}

	state.offset += count;
	output.SetCardinality(count);
}

static TableFunction CreateTSFeaturesListTableFunction() {
	TableFunction function("ts_features_list", {}, TSFeaturesListFunction);
	function.bind = TSFeaturesListBind;
	function.init_global = TSFeaturesListInit;
	return function;
}

void RegisterTSFeaturesFunction(ExtensionLoader &loader) {
	std::vector<LogicalType> variants = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DATE),
	                                     LogicalType(LogicalTypeId::BIGINT)};

	for (auto &variant : variants) {
		auto function = CreateTSFeaturesFunction(variant);
		RegisterAggregateFunctionIgnore(loader, std::move(function));
	}

	RegisterScalarFunctionIgnore(
	    loader, CreateTSFeaturesConfigFunction("ts_features_config_from_json", TSFeaturesConfigSource::JSON));
	RegisterScalarFunctionIgnore(
	    loader, CreateTSFeaturesConfigFunction("ts_features_config_from_csv", TSFeaturesConfigSource::CSV));

	auto list_function = CreateTSFeaturesListTableFunction();
	RegisterTableFunctionIgnore(loader, std::move(list_function));

	auto template_function = CreateTSFeaturesConfigTemplateFunction();
	RegisterTableFunctionIgnore(loader, std::move(template_function));
}

} // namespace duckdb
