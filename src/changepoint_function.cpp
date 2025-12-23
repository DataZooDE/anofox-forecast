#include "changepoint_function.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/parser/parsed_data/create_aggregate_function_info.hpp"
#include "duckdb/main/extension_entries.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"

// anofox-time includes
#include "anofox-time/changepoint/bocpd.hpp"

namespace duckdb {

using namespace anofoxtime;

// ============================================================================
// TS_DETECT_CHANGEPOINTS_AGG: Aggregate function returning changepoint data
// ============================================================================

// ============================================================================
// Bind data: stores parameters parsed at bind time
// ============================================================================

struct TSDetectChangepointsBindData : public FunctionData {
	double hazard_lambda = 250.0;
	bool include_probabilities = false;

	TSDetectChangepointsBindData(double hazard, bool include_probs)
	    : hazard_lambda(hazard), include_probabilities(include_probs) {
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<TSDetectChangepointsBindData>(hazard_lambda, include_probabilities);
	}

	bool Equals(const FunctionData &other) const override {
		auto &other_data = other.Cast<TSDetectChangepointsBindData>();
		return hazard_lambda == other_data.hazard_lambda && include_probabilities == other_data.include_probabilities;
	}
};

// ============================================================================
// Aggregate state: stores time series data during aggregation
// ============================================================================

struct TSDetectChangepointsData {
	std::vector<int64_t> timestamps;
	std::vector<double> values;
	bool finalized = false;
};

struct TSDetectChangepointsState {
	TSDetectChangepointsData *data;
};

struct TSDetectChangepointsOperation {
	template <class STATE>
	static void Initialize(STATE &state) {
		state.data = new TSDetectChangepointsData();
	}

	template <class STATE, class OP>
	static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
		if (!source.data || !target.data) {
			return;
		}
		target.data->timestamps.insert(target.data->timestamps.end(), source.data->timestamps.begin(),
		                               source.data->timestamps.end());
		target.data->values.insert(target.data->values.end(), source.data->values.begin(), source.data->values.end());
	}

	template <class STATE>
	static void Finalize(STATE &state, AggregateFinalizeData &finalize_data) {
		if (!state.data || state.data->timestamps.empty() || state.data->values.empty()) {
			FlatVector::SetNull(finalize_data.result, finalize_data.result_idx, true);
			return;
		}

		// Get parameters from bind data (parsed at query bind time)
		auto &bind_data = finalize_data.input.bind_data->Cast<TSDetectChangepointsBindData>();
		bool include_probs = bind_data.include_probabilities;
		double hazard_lambda = bind_data.hazard_lambda;

		// Detect changepoints using BOCPD
		std::vector<size_t> changepoint_indices;
		std::vector<double> changepoint_probabilities;

		try {
			auto detector = changepoint::BocpdDetector::builder()
			                    .hazardLambda(hazard_lambda)
			                    .normalGammaPrior({0.0, 1.0, 1.0, 1.0})
			                    .maxRunLength(1024)
			                    .build();

			if (include_probs) {
				auto result = detector.detectWithProbabilities(state.data->values);
				changepoint_indices = std::move(result.changepoint_indices);
				changepoint_probabilities = std::move(result.changepoint_probabilities);
			} else {
				changepoint_indices = detector.detect(state.data->values);
			}
		} catch (const std::exception &e) {
			FlatVector::SetNull(finalize_data.result, finalize_data.result_idx, true);
			return;
		}

		// Create changepoint lookup set
		std::unordered_set<size_t> changepoint_set(changepoint_indices.begin(), changepoint_indices.end());

		// Build result as LIST<STRUCT>
		vector<Value> result_rows;
		result_rows.reserve(state.data->values.size());

		for (size_t i = 0; i < state.data->values.size(); i++) {
			child_list_t<Value> struct_values;
			struct_values.push_back(make_pair(
			    "timestamp",
			    Value::TIMESTAMP(timestamp_t(state.data->timestamps[i] * 1000)))); // Convert ms to microseconds
			struct_values.push_back(make_pair("value", Value::DOUBLE(state.data->values[i])));
			struct_values.push_back(make_pair("is_changepoint", Value::BOOLEAN(changepoint_set.count(i) > 0)));

			// Always include probability column for consistent schema
			// Set to 0.0 when not computed (include_probs=false), actual value when computed (include_probs=true)
			double prob_value = 0.0;
			if (include_probs && i < changepoint_probabilities.size()) {
				prob_value = changepoint_probabilities[i];
			}
			struct_values.push_back(make_pair("changepoint_probability", Value::DOUBLE(prob_value)));

			result_rows.push_back(Value::STRUCT(struct_values));
		}

		// Return as LIST - let it infer type from values
		auto result_value = Value::LIST(result_rows);
		finalize_data.result.SetValue(finalize_data.result_idx, result_value);
		state.data->finalized = true;
	}

	template <class STATE>
	static void Destroy(STATE &state, AggregateInputData &) {
		if (state.data) {
			delete state.data;
			state.data = nullptr;
		}
	}
};

// ============================================================================
// Bind function: parse parameters at query bind time
// ============================================================================

static unique_ptr<FunctionData> TSDetectChangepointsBind(ClientContext &context, AggregateFunction &function,
                                                         vector<unique_ptr<Expression>> &arguments) {
	double hazard_lambda = 250.0;
	bool include_probabilities = false;

	// Parse optional params argument (index 2)
	if (arguments.size() > 2 && arguments[2]->IsFoldable()) {
		auto params_value = ExpressionExecutor::EvaluateScalar(context, *arguments[2]);

		if (!params_value.IsNull() && params_value.type().id() == LogicalTypeId::STRUCT) {
			auto &struct_children = StructValue::GetChildren(params_value);
			for (size_t i = 0; i < struct_children.size(); i++) {
				auto &key = StructType::GetChildName(params_value.type(), i);
				auto &value = struct_children[i];

				if (key == "hazard_lambda" && !value.IsNull()) {
					hazard_lambda = value.GetValue<double>();
				} else if (key == "include_probabilities" && !value.IsNull()) {
					include_probabilities = value.GetValue<bool>();
				}
			}
		}
	}

	return make_uniq<TSDetectChangepointsBindData>(hazard_lambda, include_probabilities);
}

// ============================================================================
// Update function: accumulate time series data
// ============================================================================

static void TSDetectChangepointsUpdate(Vector inputs[], AggregateInputData &aggr_input_data, idx_t input_count,
                                       Vector &state_vector, idx_t count) {
	auto &timestamp_vec = inputs[0];
	auto &value_vec = inputs[1];
	// inputs[2] is params (parsed in bind function, available via bind_data in Finalize)

	UnifiedVectorFormat timestamp_format, value_format;
	timestamp_vec.ToUnifiedFormat(count, timestamp_format);
	value_vec.ToUnifiedFormat(count, value_format);

	// State vector is FLAT - use FlatVector::GetData
	auto states = FlatVector::GetData<TSDetectChangepointsState *>(state_vector);

	for (idx_t i = 0; i < count; i++) {
		auto &state = *states[i];
		if (!state.data) {
			continue;
		}

		auto timestamp_idx = timestamp_format.sel->get_index(i);
		auto value_idx = value_format.sel->get_index(i);

		if (!timestamp_format.validity.RowIsValid(timestamp_idx) || !value_format.validity.RowIsValid(value_idx)) {
			continue;
		}

		auto timestamp_data = UnifiedVectorFormat::GetData<timestamp_t>(timestamp_format);
		auto value_data = UnifiedVectorFormat::GetData<double>(value_format);

		state.data->timestamps.push_back(Timestamp::GetEpochMs(timestamp_data[timestamp_idx]));
		state.data->values.push_back(value_data[value_idx]);
	}
}

// ============================================================================
// Registration
// ============================================================================

void RegisterChangepointFunction(ExtensionLoader &loader) {
	using STATE = TSDetectChangepointsState;
	using OP = TSDetectChangepointsOperation;

	// Return type: always include all 4 columns for consistent schema
	child_list_t<LogicalType> struct_children;
	struct_children.push_back({"timestamp", LogicalType::TIMESTAMP});
	struct_children.push_back({"value", LogicalType::DOUBLE});
	struct_children.push_back({"is_changepoint", LogicalType::BOOLEAN});
	struct_children.push_back({"changepoint_probability", LogicalType::DOUBLE});
	auto return_type = LogicalType::LIST(LogicalType::STRUCT(struct_children));

	// Create function set for overloads
	AggregateFunctionSet ts_detect_changepoints_agg_set("anofox_fcst_ts_detect_changepoints_agg");

	// 2-argument version (without params) - default behavior
	AggregateFunction agg_2arg(
	    {LogicalType::TIMESTAMP, LogicalType::DOUBLE}, return_type, AggregateFunction::StateSize<STATE>,
	    AggregateFunction::StateInitialize<STATE, OP, AggregateDestructorType::LEGACY>, TSDetectChangepointsUpdate,
	    AggregateFunction::StateCombine<STATE, OP>, AggregateFunction::StateVoidFinalize<STATE, OP>,
	    nullptr,                  // simple_update
	    TSDetectChangepointsBind, // bind function
	    AggregateFunction::StateDestroy<STATE, OP>);
	ts_detect_changepoints_agg_set.AddFunction(agg_2arg);

	// 3-argument version (with params)
	AggregateFunction agg_3arg({LogicalType::TIMESTAMP, LogicalType::DOUBLE, LogicalType::ANY}, return_type,
	                           AggregateFunction::StateSize<STATE>,
	                           AggregateFunction::StateInitialize<STATE, OP, AggregateDestructorType::LEGACY>,
	                           TSDetectChangepointsUpdate, AggregateFunction::StateCombine<STATE, OP>,
	                           AggregateFunction::StateVoidFinalize<STATE, OP>,
	                           nullptr,                  // simple_update
	                           TSDetectChangepointsBind, // bind function
	                           AggregateFunction::StateDestroy<STATE, OP>);
	ts_detect_changepoints_agg_set.AddFunction(agg_3arg);

	// Register main function
	CreateAggregateFunctionInfo main_info(ts_detect_changepoints_agg_set);
	main_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(main_info));

	// Register alias
	AggregateFunctionSet alias_set("ts_detect_changepoints_agg");
	alias_set.AddFunction(agg_2arg);
	alias_set.AddFunction(agg_3arg);
	CreateAggregateFunctionInfo alias_info(std::move(alias_set));
	alias_info.alias_of = "anofox_fcst_ts_detect_changepoints_agg";
	alias_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(alias_info));
}

} // namespace duckdb
