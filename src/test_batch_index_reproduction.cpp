// Minimal Reproduction Case for DuckDB Batch Index Collision
// This operator mimics the MSTL table-in-out operator structure but uses
// artificial delay instead of actual MSTL computation to demonstrate the
// race condition in PhysicalBatchInsert::AddCollection
//
// Usage:
//   1. Register this operator in the extension
//   2. Run: CREATE TABLE test AS SELECT * FROM test_batch_index_reproduction(TABLE large_data, 'group_col',
//   'value_col', 100);
//   3. Observe batch index collision error with large datasets

#include "test_batch_index_reproduction.hpp"
#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include <thread>
#include <chrono>
#include <unordered_map>
#include <vector>
#include <string>

namespace duckdb {

// Bind data
struct TestBatchIndexBindData : public TableFunctionData {
	std::string group_col;
	std::string value_col;
	int32_t delay_ms; // Artificial delay in milliseconds to simulate CPU-intensive computation

	idx_t group_col_idx = DConstants::INVALID_INDEX;
	idx_t value_col_idx = DConstants::INVALID_INDEX;

	vector<LogicalType> return_types;
	vector<string> return_names;
};

// Local state - accumulates data per thread
struct TestBatchIndexLocalState : public LocalTableFunctionState {
	struct GroupData {
		std::vector<double> values;
		Value group_value;
	};

	std::unordered_map<std::string, GroupData> groups;
	std::vector<std::string> group_order;
	bool input_done = false;

	// Output generation state
	idx_t current_group_idx = 0;
	idx_t current_row_idx = 0;
	std::vector<double> processed_values; // Simulated processed output
};

// Bind function
unique_ptr<FunctionData> TestBatchIndexBind(ClientContext &context, TableFunctionBindInput &input,
                                            vector<LogicalType> &return_types, vector<string> &names) {
	if (input.inputs.size() < 4) {
		throw InvalidInputException(
		    "test_batch_index_reproduction requires 4 arguments: table, group_col, value_col, delay_ms");
	}

	if (input.input_table_types.empty() || input.input_table_names.empty()) {
		throw InvalidInputException("test_batch_index_reproduction requires TABLE input");
	}

	auto bind_data = make_uniq<TestBatchIndexBindData>();
	bind_data->group_col = input.inputs[1].ToString();
	bind_data->value_col = input.inputs[2].ToString();
	bind_data->delay_ms = input.inputs[3].GetValue<int32_t>();

	// Find column indices
	for (idx_t i = 0; i < input.input_table_names.size(); i++) {
		if (input.input_table_names[i] == bind_data->group_col) {
			bind_data->group_col_idx = i;
		}
		if (input.input_table_names[i] == bind_data->value_col) {
			bind_data->value_col_idx = i;
		}
	}

	if (bind_data->group_col_idx == DConstants::INVALID_INDEX) {
		throw InvalidInputException("Column '" + bind_data->group_col + "' not found");
	}
	if (bind_data->value_col_idx == DConstants::INVALID_INDEX) {
		throw InvalidInputException("Column '" + bind_data->value_col + "' not found");
	}

	// Return types: preserve input columns + add processed_value
	return_types = input.input_table_types;
	names = input.input_table_names;
	return_types.push_back(LogicalType::DOUBLE);
	names.push_back("processed_value");

	bind_data->return_types = return_types;
	bind_data->return_names = names;

	return std::move(bind_data);
}

// Global state initialization
unique_ptr<GlobalTableFunctionState> TestBatchIndexInitGlobal(ClientContext &context, TableFunctionInitInput &input) {
	return make_uniq<GlobalTableFunctionState>();
}

// Local state initialization
unique_ptr<LocalTableFunctionState> TestBatchIndexInitLocal(ExecutionContext &context, TableFunctionInitInput &input,
                                                            GlobalTableFunctionState *global_state) {
	return make_uniq<TestBatchIndexLocalState>();
}

// InOut function - accumulates data
OperatorResultType TestBatchIndexInOut(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                       DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<TestBatchIndexBindData>();
	auto &lstate = data_p.local_state->Cast<TestBatchIndexLocalState>();

	if (input.size() == 0) {
		output.SetCardinality(0);
		return OperatorResultType::NEED_MORE_INPUT;
	}

	// Accumulate data per group
	for (idx_t i = 0; i < input.size(); i++) {
		auto group_val = input.data[bind_data.group_col_idx].GetValue(i);
		auto value_val = input.data[bind_data.value_col_idx].GetValue(i);

		if (value_val.IsNull()) {
			continue;
		}

		string group_key = group_val.ToString();
		if (lstate.groups.find(group_key) == lstate.groups.end()) {
			lstate.groups[group_key] = TestBatchIndexLocalState::GroupData();
			lstate.groups[group_key].group_value = group_val;
			lstate.group_order.push_back(group_key);
		}

		lstate.groups[group_key].values.push_back(value_val.GetValue<double>());
	}

	output.SetCardinality(0);
	return OperatorResultType::NEED_MORE_INPUT;
}

// Final function - processes groups with artificial delay
OperatorFinalizeResultType TestBatchIndexFinal(ExecutionContext &context, TableFunctionInput &data_p,
                                               DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<TestBatchIndexBindData>();
	auto &lstate = data_p.local_state->Cast<TestBatchIndexLocalState>();

	if (!lstate.input_done) {
		lstate.input_done = true;
		lstate.current_group_idx = 0;
		lstate.current_row_idx = 0;
	}

	idx_t out_count = 0;
	output.SetCardinality(0);

	if (output.ColumnCount() == 0) {
		output.InitializeEmpty(bind_data.return_types);
	}

	// Initialize output columns - set vector type to FLAT_VECTOR
	// This matches the MSTL implementation
	for (idx_t col_idx = 0; col_idx < output.ColumnCount(); col_idx++) {
		output.data[col_idx].SetVectorType(VectorType::FLAT_VECTOR);
	}

	// Early return if no groups
	if (lstate.group_order.empty()) {
		output.SetCardinality(0);
		return OperatorFinalizeResultType::FINISHED;
	}

	while (out_count < STANDARD_VECTOR_SIZE && lstate.current_group_idx < lstate.group_order.size()) {
		string group_key = lstate.group_order[lstate.current_group_idx];
		auto &group = lstate.groups[group_key];

		// Process group if not yet processed (simulate CPU-intensive computation)
		if (lstate.current_row_idx == 0) {
			// ARTIFICIAL DELAY: Simulates MSTL decomposition computation time
			// This creates variable thread completion times, exposing the race condition
			std::this_thread::sleep_for(std::chrono::milliseconds(bind_data.delay_ms));

			// "Process" the values (simple transformation for demonstration)
			lstate.processed_values.clear();
			lstate.processed_values.reserve(group.values.size());
			for (double val : group.values) {
				lstate.processed_values.push_back(val * 1.1); // Simple transformation
			}
		}

		// Emit rows
		idx_t count_in_group = group.values.size();
		while (out_count < STANDARD_VECTOR_SIZE && lstate.current_row_idx < count_in_group) {
			// Preserve original columns
			idx_t col_idx = 0;
			for (idx_t i = 0; i < bind_data.return_types.size() - 1; i++) {
				if (i == bind_data.group_col_idx) {
					output.SetValue(col_idx++, out_count, group.group_value);
				} else if (i == bind_data.value_col_idx) {
					output.SetValue(col_idx++, out_count, Value::DOUBLE(group.values[lstate.current_row_idx]));
				} else {
					// For simplicity, skip other columns in this minimal reproduction
					output.SetValue(col_idx++, out_count, Value());
				}
			}

			// Add processed value
			output.SetValue(col_idx++, out_count, Value::DOUBLE(lstate.processed_values[lstate.current_row_idx]));

			lstate.current_row_idx++;
			out_count++;
		}

		if (lstate.current_row_idx >= count_in_group) {
			lstate.current_group_idx++;
			lstate.current_row_idx = 0;
		}
	}

	output.SetCardinality(out_count);

	// Safety check
	if (out_count == 0 && lstate.current_group_idx < lstate.group_order.size()) {
		return OperatorFinalizeResultType::FINISHED;
	}

	if (lstate.current_group_idx >= lstate.group_order.size()) {
		return OperatorFinalizeResultType::FINISHED;
	}

	if (out_count > 0) {
		return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
	}

	return OperatorFinalizeResultType::FINISHED;
}

// Cardinality estimation
unique_ptr<NodeStatistics> TestBatchIndexCardinality(ClientContext &context, const FunctionData *bind_data) {
	return nullptr; // Unknown cardinality
}

// Registration function (call this from extension initialization)
void RegisterTestBatchIndexReproduction(ExtensionLoader &loader) {
	TableFunction func("test_batch_index_reproduction",
	                   {LogicalType::TABLE, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::INTEGER}, nullptr,
	                   TestBatchIndexBind, TestBatchIndexInitGlobal, TestBatchIndexInitLocal);

	func.in_out_function = TestBatchIndexInOut;
	func.in_out_function_final = TestBatchIndexFinal;
	func.cardinality = TestBatchIndexCardinality;

	TableFunctionSet set("test_batch_index_reproduction");
	set.AddFunction(func);
	CreateTableFunctionInfo info(std::move(set));
	loader.RegisterFunction(std::move(info));
}

} // namespace duckdb
