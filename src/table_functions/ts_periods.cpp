#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

// Helper to extract list as double values
static void ExtractListAsDouble(Vector &list_vec, idx_t row_idx, vector<double> &out_values) {
    auto list_data = ListVector::GetData(list_vec);
    auto &list_entry = list_data[row_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);
    auto child_data = FlatVector::GetData<double>(child_vec);
    auto &child_validity = FlatVector::Validity(child_vec);

    out_values.clear();
    out_values.reserve(list_entry.length);

    for (idx_t i = 0; i < list_entry.length; i++) {
        idx_t child_idx = list_entry.offset + i;
        if (child_validity.RowIsValid(child_idx)) {
            out_values.push_back(child_data[child_idx]);
        }
    }
}

// ============================================================================
// ts_detect_periods - Detect periods using specified method
// Returns: STRUCT(periods STRUCT[], n_periods, primary_period, method)
// ============================================================================

static LogicalType GetMultiPeriodResultType() {
    // Inner struct for each detected period
    child_list_t<LogicalType> period_children;
    period_children.push_back(make_pair("period", LogicalType::DOUBLE));
    period_children.push_back(make_pair("confidence", LogicalType::DOUBLE));
    period_children.push_back(make_pair("strength", LogicalType::DOUBLE));
    period_children.push_back(make_pair("amplitude", LogicalType::DOUBLE));
    period_children.push_back(make_pair("phase", LogicalType::DOUBLE));
    period_children.push_back(make_pair("iteration", LogicalType::BIGINT));
    auto period_type = LogicalType::STRUCT(std::move(period_children));

    // Outer result struct
    child_list_t<LogicalType> children;
    children.push_back(make_pair("periods", LogicalType::LIST(period_type)));
    children.push_back(make_pair("n_periods", LogicalType::BIGINT));
    children.push_back(make_pair("primary_period", LogicalType::DOUBLE));
    children.push_back(make_pair("method", LogicalType::VARCHAR));
    return LogicalType::STRUCT(std::move(children));
}

static void TsDetectPeriodsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto &method_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, row_idx, values);

        const char *method_str = nullptr;
        if (!FlatVector::IsNull(method_vec, row_idx)) {
            auto method_data = FlatVector::GetData<string_t>(method_vec);
            method_str = method_data[row_idx].GetData();
        }

        MultiPeriodResult period_result;
        memset(&period_result, 0, sizeof(period_result));
        AnofoxError error;

        bool success = anofox_ts_detect_periods(
            values.data(),
            values.size(),
            method_str,
            &period_result,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_detect_periods failed: %s", error.message);
        }

        auto &children = StructVector::GetEntries(result);

        // Set periods list - IMPORTANT: Reserve before getting child references
        // as Reserve can relocate the underlying memory
        {
            auto &periods_list = *children[0];
            auto current_size = ListVector::GetListSize(periods_list);

            // First, reserve space and set list size
            ListVector::Reserve(periods_list, current_size + period_result.n_periods);
            ListVector::SetListSize(periods_list, current_size + period_result.n_periods);

            // Now get the list data and child references AFTER Reserve
            auto list_data = FlatVector::GetData<list_entry_t>(periods_list);
            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = period_result.n_periods;

            // Get child vector references AFTER Reserve to avoid dangling references
            auto &list_child = ListVector::GetEntry(periods_list);
            auto &struct_entries = StructVector::GetEntries(list_child);
            for (size_t i = 0; i < period_result.n_periods; i++) {
                FlatVector::GetData<double>(*struct_entries[0])[current_size + i] = period_result.periods[i].period;
                FlatVector::GetData<double>(*struct_entries[1])[current_size + i] = period_result.periods[i].confidence;
                FlatVector::GetData<double>(*struct_entries[2])[current_size + i] = period_result.periods[i].strength;
                FlatVector::GetData<double>(*struct_entries[3])[current_size + i] = period_result.periods[i].amplitude;
                FlatVector::GetData<double>(*struct_entries[4])[current_size + i] = period_result.periods[i].phase;
                FlatVector::GetData<int64_t>(*struct_entries[5])[current_size + i] = period_result.periods[i].iteration;
            }
        }

        // Set scalar fields
        FlatVector::GetData<int64_t>(*children[1])[row_idx] = period_result.n_periods;
        FlatVector::GetData<double>(*children[2])[row_idx] = period_result.primary_period;
        FlatVector::GetData<string_t>(*children[3])[row_idx] = StringVector::AddString(*children[3], period_result.method);

        anofox_free_multi_period_result(&period_result);
    }
}

// Single-argument convenience version
static void TsDetectPeriodsSimpleFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, row_idx, values);

        MultiPeriodResult period_result;
        memset(&period_result, 0, sizeof(period_result));
        AnofoxError error;

        bool success = anofox_ts_detect_periods(
            values.data(),
            values.size(),
            nullptr,  // default method
            &period_result,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_detect_periods failed: %s", error.message);
        }

        auto &children = StructVector::GetEntries(result);

        // Set periods list - IMPORTANT: Reserve before getting child references
        // as Reserve can relocate the underlying memory
        {
            auto &periods_list = *children[0];
            auto current_size = ListVector::GetListSize(periods_list);

            // First, reserve space and set list size
            ListVector::Reserve(periods_list, current_size + period_result.n_periods);
            ListVector::SetListSize(periods_list, current_size + period_result.n_periods);

            // Now get the list data and child references AFTER Reserve
            auto list_data = FlatVector::GetData<list_entry_t>(periods_list);
            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = period_result.n_periods;

            // Get child vector references AFTER Reserve to avoid dangling references
            auto &list_child = ListVector::GetEntry(periods_list);
            auto &struct_entries = StructVector::GetEntries(list_child);
            for (size_t i = 0; i < period_result.n_periods; i++) {
                FlatVector::GetData<double>(*struct_entries[0])[current_size + i] = period_result.periods[i].period;
                FlatVector::GetData<double>(*struct_entries[1])[current_size + i] = period_result.periods[i].confidence;
                FlatVector::GetData<double>(*struct_entries[2])[current_size + i] = period_result.periods[i].strength;
                FlatVector::GetData<double>(*struct_entries[3])[current_size + i] = period_result.periods[i].amplitude;
                FlatVector::GetData<double>(*struct_entries[4])[current_size + i] = period_result.periods[i].phase;
                FlatVector::GetData<int64_t>(*struct_entries[5])[current_size + i] = period_result.periods[i].iteration;
            }
        }

        // Set scalar fields
        FlatVector::GetData<int64_t>(*children[1])[row_idx] = period_result.n_periods;
        FlatVector::GetData<double>(*children[2])[row_idx] = period_result.primary_period;
        FlatVector::GetData<string_t>(*children[3])[row_idx] = StringVector::AddString(*children[3], period_result.method);

        anofox_free_multi_period_result(&period_result);
    }
}

void RegisterTsDetectPeriodsFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_periods_set("ts_detect_periods");
    // Single-argument version (values only, default method)
    ts_periods_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        GetMultiPeriodResultType(),
        TsDetectPeriodsSimpleFunction
    ));
    // Two-argument version (values, method)
    ts_periods_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        GetMultiPeriodResultType(),
        TsDetectPeriodsFunction
    ));
    loader.RegisterFunction(ts_periods_set);
}

// ============================================================================
// ts_estimate_period_fft - Estimate single period using FFT
// Returns: STRUCT(period, frequency, power, confidence, method)
// ============================================================================

static LogicalType GetSinglePeriodResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("period", LogicalType::DOUBLE));
    children.push_back(make_pair("frequency", LogicalType::DOUBLE));
    children.push_back(make_pair("power", LogicalType::DOUBLE));
    children.push_back(make_pair("confidence", LogicalType::DOUBLE));
    children.push_back(make_pair("method", LogicalType::VARCHAR));
    return LogicalType::STRUCT(std::move(children));
}

static void TsEstimatePeriodFftFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, row_idx, values);

        SinglePeriodResult period_result;
        memset(&period_result, 0, sizeof(period_result));
        AnofoxError error;

        bool success = anofox_ts_estimate_period_fft(
            values.data(),
            values.size(),
            &period_result,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_estimate_period_fft failed: %s", error.message);
        }

        auto &children = StructVector::GetEntries(result);
        FlatVector::GetData<double>(*children[0])[row_idx] = period_result.period;
        FlatVector::GetData<double>(*children[1])[row_idx] = period_result.frequency;
        FlatVector::GetData<double>(*children[2])[row_idx] = period_result.power;
        FlatVector::GetData<double>(*children[3])[row_idx] = period_result.confidence;
        FlatVector::GetData<string_t>(*children[4])[row_idx] = StringVector::AddString(*children[4], period_result.method);
    }
}

void RegisterTsEstimatePeriodFftFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_period_fft_set("ts_estimate_period_fft");
    ts_period_fft_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        GetSinglePeriodResultType(),
        TsEstimatePeriodFftFunction
    ));
    loader.RegisterFunction(ts_period_fft_set);
}

// ============================================================================
// ts_estimate_period_acf - Estimate single period using ACF
// Returns: STRUCT(period, frequency, power, confidence, method)
// ============================================================================

static void TsEstimatePeriodAcfFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    // Check for optional max_lag parameter
    int max_lag = 0;  // 0 means auto
    if (args.ColumnCount() > 1 && !FlatVector::IsNull(args.data[1], 0)) {
        max_lag = FlatVector::GetData<int32_t>(args.data[1])[0];
    }

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, row_idx, values);

        SinglePeriodResult period_result;
        memset(&period_result, 0, sizeof(period_result));
        AnofoxError error;

        bool success = anofox_ts_estimate_period_acf(
            values.data(),
            values.size(),
            max_lag,
            &period_result,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_estimate_period_acf failed: %s", error.message);
        }

        auto &children = StructVector::GetEntries(result);
        FlatVector::GetData<double>(*children[0])[row_idx] = period_result.period;
        FlatVector::GetData<double>(*children[1])[row_idx] = period_result.frequency;
        FlatVector::GetData<double>(*children[2])[row_idx] = period_result.power;
        FlatVector::GetData<double>(*children[3])[row_idx] = period_result.confidence;
        FlatVector::GetData<string_t>(*children[4])[row_idx] = StringVector::AddString(*children[4], period_result.method);
    }
}

void RegisterTsEstimatePeriodAcfFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_period_acf_set("ts_estimate_period_acf");
    // Single-argument version
    ts_period_acf_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        GetSinglePeriodResultType(),
        TsEstimatePeriodAcfFunction
    ));
    // Two-argument version with max_lag
    ts_period_acf_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER},
        GetSinglePeriodResultType(),
        TsEstimatePeriodAcfFunction
    ));
    loader.RegisterFunction(ts_period_acf_set);
}

// ============================================================================
// ts_detect_multiple_periods - Detect multiple periods with filtering
// Returns: STRUCT(periods STRUCT[], n_periods, primary_period, method)
// ============================================================================

static void TsDetectMultiplePeriodsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    // Optional parameters
    int max_periods = 0;
    double min_confidence = 0.0;
    double min_strength = 0.0;

    if (args.ColumnCount() > 1 && !FlatVector::IsNull(args.data[1], 0)) {
        max_periods = FlatVector::GetData<int32_t>(args.data[1])[0];
    }
    if (args.ColumnCount() > 2 && !FlatVector::IsNull(args.data[2], 0)) {
        min_confidence = FlatVector::GetData<double>(args.data[2])[0];
    }
    if (args.ColumnCount() > 3 && !FlatVector::IsNull(args.data[3], 0)) {
        min_strength = FlatVector::GetData<double>(args.data[3])[0];
    }

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, row_idx, values);

        MultiPeriodResult period_result;
        memset(&period_result, 0, sizeof(period_result));
        AnofoxError error;

        bool success = anofox_ts_detect_multiple_periods(
            values.data(),
            values.size(),
            max_periods,
            min_confidence,
            min_strength,
            &period_result,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_detect_multiple_periods failed: %s", error.message);
        }

        auto &children = StructVector::GetEntries(result);

        // Set periods list - IMPORTANT: Reserve before getting child references
        // as Reserve can relocate the underlying memory
        {
            auto &periods_list = *children[0];
            auto current_size = ListVector::GetListSize(periods_list);

            // First, reserve space and set list size
            ListVector::Reserve(periods_list, current_size + period_result.n_periods);
            ListVector::SetListSize(periods_list, current_size + period_result.n_periods);

            // Now get the list data and child references AFTER Reserve
            auto list_data = FlatVector::GetData<list_entry_t>(periods_list);
            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = period_result.n_periods;

            // Get child vector references AFTER Reserve to avoid dangling references
            auto &list_child = ListVector::GetEntry(periods_list);
            auto &struct_entries = StructVector::GetEntries(list_child);
            for (size_t i = 0; i < period_result.n_periods; i++) {
                FlatVector::GetData<double>(*struct_entries[0])[current_size + i] = period_result.periods[i].period;
                FlatVector::GetData<double>(*struct_entries[1])[current_size + i] = period_result.periods[i].confidence;
                FlatVector::GetData<double>(*struct_entries[2])[current_size + i] = period_result.periods[i].strength;
                FlatVector::GetData<double>(*struct_entries[3])[current_size + i] = period_result.periods[i].amplitude;
                FlatVector::GetData<double>(*struct_entries[4])[current_size + i] = period_result.periods[i].phase;
                FlatVector::GetData<int64_t>(*struct_entries[5])[current_size + i] = period_result.periods[i].iteration;
            }
        }

        // Set scalar fields
        FlatVector::GetData<int64_t>(*children[1])[row_idx] = period_result.n_periods;
        FlatVector::GetData<double>(*children[2])[row_idx] = period_result.primary_period;
        FlatVector::GetData<string_t>(*children[3])[row_idx] = StringVector::AddString(*children[3], period_result.method);

        anofox_free_multi_period_result(&period_result);
    }
}

void RegisterTsDetectMultiplePeriodsFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_multi_periods_set("ts_detect_multiple_periods");
    // Single-argument version
    ts_multi_periods_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        GetMultiPeriodResultType(),
        TsDetectMultiplePeriodsFunction
    ));
    // With max_periods
    ts_multi_periods_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER},
        GetMultiPeriodResultType(),
        TsDetectMultiplePeriodsFunction
    ));
    // With max_periods, min_confidence
    ts_multi_periods_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER, LogicalType::DOUBLE},
        GetMultiPeriodResultType(),
        TsDetectMultiplePeriodsFunction
    ));
    // With max_periods, min_confidence, min_strength
    ts_multi_periods_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER, LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetMultiPeriodResultType(),
        TsDetectMultiplePeriodsFunction
    ));
    loader.RegisterFunction(ts_multi_periods_set);
}

} // namespace duckdb
