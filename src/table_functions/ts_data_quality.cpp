#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/types/vector.hpp"
#include "ts_list_marshal.hpp"

namespace duckdb {

static LogicalType GetDataQualityResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("structural_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("temporal_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("magnitude_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("behavioral_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("overall_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("n_gaps", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_missing", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("is_constant", LogicalType(LogicalTypeId::BOOLEAN)));
    return LogicalType::STRUCT(std::move(children));
}

template <typename T>
static void SetStructField(Vector &result, idx_t field_idx, idx_t row_idx, T value) {
    auto &children = StructVector::GetEntries(result);
    auto data = FlatVector::GetData<T>(*children[field_idx]);
    data[row_idx] = value;
}

static void TsDataQualityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    ListDoubleReader list_reader(list_vec, count);
    vector<double> values;
    vector<uint64_t> validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (list_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        list_reader.Read(row_idx, values, validity);

        DataQualityResult dq_result;
        AnofoxError error;

        bool success = anofox_ts_data_quality(
            values.data(),
            validity.empty() ? nullptr : validity.data(),
            values.size(),
            &dq_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        SetStructField<double>(result, 0, row_idx, dq_result.structural_score);
        SetStructField<double>(result, 1, row_idx, dq_result.temporal_score);
        SetStructField<double>(result, 2, row_idx, dq_result.magnitude_score);
        SetStructField<double>(result, 3, row_idx, dq_result.behavioral_score);
        SetStructField<double>(result, 4, row_idx, dq_result.overall_score);
        SetStructField<uint64_t>(result, 5, row_idx, dq_result.n_gaps);
        SetStructField<uint64_t>(result, 6, row_idx, dq_result.n_missing);
        SetStructField<bool>(result, 7, row_idx, dq_result.is_constant);
    }
}

void RegisterTsDataQualityFunction(ExtensionLoader &loader) {
    // Internal scalar function used by ts_data_quality table macro
    // Named with underscore prefix to match C++ API (ts_data_quality is table macro only)
    ScalarFunctionSet ts_dq_set("_ts_data_quality");
    ts_dq_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetDataQualityResultType(),
        TsDataQualityFunction
    ));

    // Mark as internal to hide from duckdb_functions() and deprioritize in autocomplete
    CreateScalarFunctionInfo info(ts_dq_set);
    info.internal = true;
    loader.RegisterFunction(info);
}

// Placeholder for summary function
void RegisterTsDataQualitySummaryFunction(ExtensionLoader &loader) {
    // TODO: Implement aggregate version
}

} // namespace duckdb
