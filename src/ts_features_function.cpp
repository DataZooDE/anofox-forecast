#include "ts_features_function.hpp"
#include "anofox-time/features/feature_types.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include <algorithm>
#include <cmath>

namespace duckdb {

using namespace anofoxtime::features;

struct TSFeaturesSample {
	int64_t key;
	double value;
	idx_t ordinal;
};

struct TSFeaturesData {
	std::vector<TSFeaturesSample> samples;
};

struct TSFeaturesAggregateState {
	TSFeaturesData *data;

	TSFeaturesAggregateState() : data(nullptr) {
	}
};

struct TSFeaturesBindData : public FunctionData {
	LogicalTypeId timestamp_type;
	bool has_config;
	FeatureConfig config;
	std::vector<std::string> column_names;
	LogicalType return_type;

	unique_ptr<FunctionData> Copy() const override {
		auto copy = make_uniq<TSFeaturesBindData>();
		copy->timestamp_type = timestamp_type;
		copy->has_config = has_config;
		copy->config = config;
		copy->column_names = column_names;
		copy->return_type = return_type;
		return copy;
	}

	bool Equals(const FunctionData &other_p) const override {
		auto &other = other_p.Cast<TSFeaturesBindData>();
		return other.timestamp_type == timestamp_type && other.has_config == has_config &&
		       other.column_names == column_names;
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
	auto &bind_data = aggr_input.bind_data->Cast<TSFeaturesBindData>();
	UnifiedVectorFormat ts_format;
	UnifiedVectorFormat value_format;
	inputs[0].ToUnifiedFormat(count, ts_format);
	inputs[1].ToUnifiedFormat(count, value_format);
	UnifiedVectorFormat config_format;
	if (bind_data.has_config) {
		inputs[2].ToUnifiedFormat(count, config_format);
	}

	auto states = FlatVector::GetData<TSFeaturesAggregateState *>(state_vector);
	for (idx_t i = 0; i < count; ++i) {
		auto &state = *states[i];
		if (!state.data) {
			continue;
		}
		auto ts_idx = ts_format.sel->get_index(i);
		auto val_idx = value_format.sel->get_index(i);
		if (!ts_format.validity.RowIsValid(ts_idx) || !value_format.validity.RowIsValid(val_idx)) {
			continue;
		}
		if (bind_data.has_config) {
			auto cfg_idx = config_format.sel->get_index(i);
			if (config_format.validity.RowIsValid(cfg_idx)) {
				throw InvalidInputException("Custom configuration for ts_features is not supported yet");
			}
		}
		int64_t key = ConvertTimestampToKey(ts_format, ts_idx, bind_data.timestamp_type);
		auto values = UnifiedVectorFormat::GetData<double>(value_format);
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
		state.data = new TSFeaturesData();
	}

	template <class STATE, class OP>
	static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
		if (!source.data || !target.data) {
			return;
		}
		target.data->samples.insert(target.data->samples.end(), source.data->samples.begin(),
		                            source.data->samples.end());
	}

	template <class STATE>
	static void Finalize(STATE &state, AggregateFinalizeData &finalize_data) {
		auto &bind_data = finalize_data.input.bind_data->Cast<TSFeaturesBindData>();

		child_list_t<Value> children;
		children.reserve(bind_data.column_names.size());

		if (!state.data || state.data->samples.empty()) {
			for (const auto &name : bind_data.column_names) {
				children.emplace_back(name, Value());
			}
			finalize_data.result.SetValue(finalize_data.result_idx, Value::STRUCT(std::move(children)));
			return;
		}

		auto &samples = state.data->samples;
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

		finalize_data.result.SetValue(finalize_data.result_idx, Value::STRUCT(std::move(children)));
	}

	template <class STATE>
	static void Destroy(STATE &state, AggregateInputData &) {
		if (state.data) {
			delete state.data;
			state.data = nullptr;
		}
	}
};

static unique_ptr<FunctionData> TSFeaturesBind(ClientContext &, AggregateFunction &function,
                                               vector<unique_ptr<Expression>> &arguments) {
	if (arguments.size() < 2 || arguments.size() > 3) {
		throw InvalidInputException("ts_features expects 2 or 3 arguments (timestamp, value, [config])");
	}

	auto timestamp_type_id = arguments[0]->return_type.id();
	if (timestamp_type_id != LogicalTypeId::TIMESTAMP && timestamp_type_id != LogicalTypeId::DATE &&
	    timestamp_type_id != LogicalTypeId::BIGINT) {
		throw InvalidInputException("ts_features timestamp argument must be TIMESTAMP, DATE, or BIGINT");
	}

	auto bind_data = make_uniq<TSFeaturesBindData>();
	bind_data->timestamp_type = timestamp_type_id;
	bind_data->has_config = arguments.size() == 3;
	bind_data->config = FeatureRegistry::Instance().DefaultConfig();

	child_list_t<LogicalType> children;
	for (const auto &request : bind_data->config.requests) {
		auto params = request.parameters.empty() ? std::vector<ParameterMap> {ParameterMap {}} : request.parameters;
		for (const auto &param : params) {
			auto column_name = request.name + param.ToSuffixString();
			bind_data->column_names.push_back(column_name);
			children.emplace_back(column_name, LogicalType::DOUBLE);
		}
	}

	bind_data->return_type = LogicalType::STRUCT(std::move(children));
	function.return_type = bind_data->return_type;

	return bind_data;
}

AggregateFunction CreateTSFeaturesFunction(const LogicalType &timestamp_type, bool has_config) {
	using STATE = TSFeaturesAggregateState;
	using OP = TSFeaturesAggregateOperation;

	std::vector<LogicalType> arguments;
	arguments.push_back(timestamp_type);
	arguments.push_back(LogicalType::DOUBLE);
	if (has_config) {
		arguments.push_back(LogicalType::ANY);
	}

	AggregateFunction function(
	    "ts_features", std::move(arguments), LogicalType::STRUCT({}), AggregateFunction::StateSize<STATE>,
	    AggregateFunction::StateInitialize<STATE, OP, AggregateDestructorType::LEGACY>, TSFeaturesUpdate,
	    AggregateFunction::StateCombine<STATE, OP>, AggregateFunction::StateVoidFinalize<STATE, OP>, nullptr,
	    TSFeaturesBind, AggregateFunction::StateDestroy<STATE, OP>);
	function.order_dependent = AggregateOrderDependent::ORDER_DEPENDENT;
	return function;
}

void RegisterTSFeaturesFunction(ExtensionLoader &loader) {
	std::vector<std::pair<LogicalType, bool>> variants = {
	    {LogicalType::TIMESTAMP, false}, {LogicalType::TIMESTAMP, true}, {LogicalType::DATE, false},
	    {LogicalType::DATE, true},       {LogicalType::BIGINT, false},   {LogicalType::BIGINT, true},
	};

	for (auto &variant : variants) {
		auto function = CreateTSFeaturesFunction(variant.first, variant.second);
		loader.RegisterFunction(function);
	}
}

} // namespace duckdb
