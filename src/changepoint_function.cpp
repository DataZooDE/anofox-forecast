#include "changepoint_function.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/main/extension_entries.hpp"

// anofox-time includes
#include "anofox-time/changepoint/bocpd.hpp"

namespace duckdb {

using namespace anofoxtime;

// ============================================================================
// TS_DETECT_CHANGEPOINTS_AGG: Aggregate function returning changepoint data
// ============================================================================

struct TSDetectChangepointsData {
	std::vector<int64_t> timestamps;
	std::vector<double> values;
	double hazard_lambda = 250.0;
	bool include_probabilities = false; // Default: don't compute probabilities (faster)
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
		target.data->hazard_lambda = source.data->hazard_lambda;
	}

	template <class STATE>
	static void Finalize(STATE &state, AggregateFinalizeData &finalize_data) {
		if (!state.data || state.data->timestamps.empty() || state.data->values.empty()) {
			FlatVector::SetNull(finalize_data.result, finalize_data.result_idx, true);
			return;
		}

		// Detect changepoints using BOCPD
		std::vector<size_t> changepoint_indices;
		std::vector<double> changepoint_probabilities;

		try {
			auto detector = changepoint::BocpdDetector::builder()
			                    .hazardLambda(state.data->hazard_lambda)
			                    .normalGammaPrior({0.0, 1.0, 1.0, 1.0})
			                    .maxRunLength(1024)
			                    .build();

			if (state.data->include_probabilities) {
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

			// Always include probability column
			double prob_value = 0.0;
			if (state.data->include_probabilities && i < changepoint_probabilities.size()) {
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

// Update function for aggregate
static void TSDetectChangepointsUpdate(Vector inputs[], AggregateInputData &aggr_input_data, idx_t input_count,
                                       Vector &state_vector, idx_t count) {
	auto &timestamp_vec = inputs[0];
	auto &value_vec = inputs[1];
	// inputs[2] is params (constant, handled once on first call)

	UnifiedVectorFormat timestamp_format, value_format;
	timestamp_vec.ToUnifiedFormat(count, timestamp_format);
	value_vec.ToUnifiedFormat(count, value_format);

	// State vector is FLAT - use FlatVector::GetData
	auto states = FlatVector::GetData<TSDetectChangepointsState *>(state_vector);

	// Parse parameters from first row (they're constant across the aggregate)
	if (input_count > 2 && count > 0 && states[0]->data && !states[0]->data->finalized) {
		auto &params_vec = inputs[2];
		if (params_vec.GetType().id() == LogicalTypeId::MAP) {
			auto params_value = params_vec.GetValue(0);
			if (!params_value.IsNull()) {
				auto &struct_children = StructValue::GetChildren(params_value);
				for (size_t i = 0; i < struct_children.size(); i++) {
					auto &key = StructType::GetChildName(params_value.type(), i);
					auto &value = struct_children[i];

					if (key == "hazard_lambda" && !value.IsNull()) {
						try {
							states[0]->data->hazard_lambda = value.template GetValue<double>();
						} catch (...) {
							// Keep default on error
						}
					} else if (key == "include_probabilities" && !value.IsNull()) {
						try {
							states[0]->data->include_probabilities = value.template GetValue<bool>();
						} catch (...) {
							// Keep default on error
						}
					}
				}
			}
		}
	}

	for (idx_t i = 0; i < count; i++) {
		auto &state = *states[i];
		if (!state.data) {
			continue;
		}

		// Copy parameters from first state to all others (for combine)
		if (i > 0 && states[0]->data) {
			state.data->hazard_lambda = states[0]->data->hazard_lambda;
			state.data->include_probabilities = states[0]->data->include_probabilities;
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

// Create the aggregate function
AggregateFunction CreateTSDetectChangepointsAgg() {
	using STATE = TSDetectChangepointsState;
	using OP = TSDetectChangepointsOperation;

	// Return type: LIST<STRUCT(timestamp TIMESTAMP, value DOUBLE, is_changepoint BOOLEAN, changepoint_probability
	// DOUBLE)>
	child_list_t<LogicalType> struct_children;
	struct_children.push_back({"timestamp", LogicalType::TIMESTAMP});
	struct_children.push_back({"value", LogicalType::DOUBLE});
	struct_children.push_back({"is_changepoint", LogicalType::BOOLEAN});
	struct_children.push_back({"changepoint_probability", LogicalType::DOUBLE}); // Always include

	auto return_type = LogicalType::LIST(LogicalType::STRUCT(struct_children));

	AggregateFunction ts_detect_changepoints_agg(
	    "ts_detect_changepoints_agg", {LogicalType::TIMESTAMP, LogicalType::DOUBLE, LogicalType::ANY}, // Added params
	    return_type, AggregateFunction::StateSize<STATE>,
	    AggregateFunction::StateInitialize<STATE, OP, AggregateDestructorType::LEGACY>, TSDetectChangepointsUpdate,
	    AggregateFunction::StateCombine<STATE, OP>, AggregateFunction::StateVoidFinalize<STATE, OP>,
	    nullptr, // simple_update
	    nullptr, // bind
	    AggregateFunction::StateDestroy<STATE, OP>);

	return ts_detect_changepoints_agg;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterChangepointFunction(ExtensionLoader &loader) {
	// Register the aggregate function
	auto ts_detect_changepoints_agg = CreateTSDetectChangepointsAgg();
	loader.RegisterFunction(ts_detect_changepoints_agg);
}

} // namespace duckdb
