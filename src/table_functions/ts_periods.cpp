#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

// Helper to extract list as double values
// Uses UnifiedVectorFormat to handle all vector types (flat, constant, dictionary)
static void ExtractListAsDouble(Vector &list_vec, idx_t count, idx_t row_idx, vector<double> &out_values) {
    // Use UnifiedVectorFormat to handle all vector types
    UnifiedVectorFormat list_format;
    list_vec.ToUnifiedFormat(count, list_format);

    auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(list_format);
    auto list_idx = list_format.sel->get_index(row_idx);
    auto &list_entry = list_entries[list_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);

    // Also use UnifiedVectorFormat for child vector
    UnifiedVectorFormat child_format;
    child_vec.ToUnifiedFormat(ListVector::GetListSize(list_vec), child_format);
    auto child_values = UnifiedVectorFormat::GetData<double>(child_format);

    out_values.clear();
    out_values.reserve(list_entry.length);

    for (idx_t i = 0; i < list_entry.length; i++) {
        idx_t child_idx = list_entry.offset + i;
        auto unified_child_idx = child_format.sel->get_index(child_idx);
        if (child_format.validity.RowIsValid(unified_child_idx)) {
            out_values.push_back(child_values[unified_child_idx]);
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

    // Two-pass approach to avoid incremental Reserve calls which can cause memory issues:
    // Pass 1: Compute all results and store them, calculate total list size
    // Pass 2: Reserve once, then copy all data

    struct RowResult {
        FlatMultiPeriodResult result;
        bool is_null;
    };

    vector<RowResult> row_results(count);
    size_t total_periods = 0;

    // Pass 1: Compute all FFI results
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        row_results[row_idx].is_null = false;
        memset(&row_results[row_idx].result, 0, sizeof(FlatMultiPeriodResult));

        if (FlatVector::IsNull(values_vec, row_idx)) {
            row_results[row_idx].is_null = true;
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, count, row_idx, values);

        const char *method_str = nullptr;
        if (!FlatVector::IsNull(method_vec, row_idx)) {
            auto method_data = FlatVector::GetData<string_t>(method_vec);
            method_str = method_data[row_idx].GetData();
        }

        AnofoxError error;
        bool success = anofox_ts_detect_periods_flat(
            values.data(),
            values.size(),
            method_str,
            &row_results[row_idx].result,
            &error
        );

        if (!success) {
            // Clean up any already-allocated results
            for (idx_t i = 0; i < row_idx; i++) {
                if (!row_results[i].is_null) {
                    anofox_free_flat_multi_period_result(&row_results[i].result);
                }
            }
            throw InvalidInputException("ts_detect_periods failed: %s", error.message);
        }

        total_periods += row_results[row_idx].result.n_periods;
    }

    // Pass 2: Reserve all space at once, then copy data
    // IMPORTANT: Get fresh references after each operation that might reallocate
    {
        auto &children_init = StructVector::GetEntries(result);
        auto &periods_list_init = *children_init[0];

        // Reserve all space at once (reserve at least 1 to ensure valid list structure)
        ListVector::Reserve(periods_list_init, total_periods > 0 ? total_periods : 1);
        ListVector::SetListSize(periods_list_init, total_periods);
    }

    // Get fresh references after Reserve (Reserve may have reallocated memory)
    auto &children = StructVector::GetEntries(result);
    auto &periods_list = *children[0];

    // Get list_data pointer for the list entries
    auto list_data = FlatVector::GetData<list_entry_t>(periods_list);

    // Only get struct child pointers if we have periods to copy
    double *period_data = nullptr;
    double *confidence_data = nullptr;
    double *strength_data = nullptr;
    double *amplitude_data = nullptr;
    double *phase_data = nullptr;
    int64_t *iteration_data = nullptr;

    if (total_periods > 0) {
        auto &list_child = ListVector::GetEntry(periods_list);
        auto &struct_entries = StructVector::GetEntries(list_child);
        period_data = FlatVector::GetData<double>(*struct_entries[0]);
        confidence_data = FlatVector::GetData<double>(*struct_entries[1]);
        strength_data = FlatVector::GetData<double>(*struct_entries[2]);
        amplitude_data = FlatVector::GetData<double>(*struct_entries[3]);
        phase_data = FlatVector::GetData<double>(*struct_entries[4]);
        iteration_data = FlatVector::GetData<int64_t>(*struct_entries[5]);
    }

    auto n_periods_data = FlatVector::GetData<int64_t>(*children[1]);
    auto primary_period_data = FlatVector::GetData<double>(*children[2]);

    size_t current_offset = 0;
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (row_results[row_idx].is_null) {
            list_data[row_idx].offset = current_offset;
            list_data[row_idx].length = 0;
            continue;
        }

        auto &res = row_results[row_idx].result;

        // Set list entry
        list_data[row_idx].offset = current_offset;
        list_data[row_idx].length = res.n_periods;

        // Copy period data (only if we have data pointers)
        if (res.n_periods > 0 && period_data != nullptr) {
            for (size_t i = 0; i < res.n_periods; i++) {
                period_data[current_offset + i] = res.period_values[i];
                confidence_data[current_offset + i] = res.confidence_values[i];
                strength_data[current_offset + i] = res.strength_values[i];
                amplitude_data[current_offset + i] = res.amplitude_values[i];
                phase_data[current_offset + i] = res.phase_values[i];
                iteration_data[current_offset + i] = res.iteration_values[i];
            }
        }

        current_offset += res.n_periods;

        // Set scalar fields
        n_periods_data[row_idx] = res.n_periods;
        primary_period_data[row_idx] = res.primary_period;
        FlatVector::GetData<string_t>(*children[3])[row_idx] = StringVector::AddString(*children[3], res.method);

        // Free FFI result
        anofox_free_flat_multi_period_result(&row_results[row_idx].result);
    }
}

// Single-argument convenience version
static void TsDetectPeriodsSimpleFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Two-pass approach to avoid incremental Reserve calls which can cause memory issues
    struct RowResult {
        FlatMultiPeriodResult result;
        bool is_null;
    };

    vector<RowResult> row_results(count);
    size_t total_periods = 0;

    // Pass 1: Compute all FFI results
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        row_results[row_idx].is_null = false;
        memset(&row_results[row_idx].result, 0, sizeof(FlatMultiPeriodResult));

        if (FlatVector::IsNull(values_vec, row_idx)) {
            row_results[row_idx].is_null = true;
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, count, row_idx, values);

        AnofoxError error;
        bool success = anofox_ts_detect_periods_flat(
            values.data(),
            values.size(),
            nullptr,  // default method
            &row_results[row_idx].result,
            &error
        );

        if (!success) {
            // Clean up any already-allocated results
            for (idx_t i = 0; i < row_idx; i++) {
                if (!row_results[i].is_null) {
                    anofox_free_flat_multi_period_result(&row_results[i].result);
                }
            }
            throw InvalidInputException("ts_detect_periods failed: %s", error.message);
        }

        total_periods += row_results[row_idx].result.n_periods;
    }

    // Pass 2: Reserve all space at once, then copy data
    // IMPORTANT: Get fresh references after each operation that might reallocate
    {
        auto &children_init = StructVector::GetEntries(result);
        auto &periods_list_init = *children_init[0];

        // Reserve all space at once (reserve at least 1 to ensure valid list structure)
        ListVector::Reserve(periods_list_init, total_periods > 0 ? total_periods : 1);
        ListVector::SetListSize(periods_list_init, total_periods);
    }

    // Get fresh references after Reserve (Reserve may have reallocated memory)
    auto &children = StructVector::GetEntries(result);
    auto &periods_list = *children[0];

    // Get list_data pointer for the list entries
    auto list_data = FlatVector::GetData<list_entry_t>(periods_list);

    // Only get struct child pointers if we have periods to copy
    double *period_data = nullptr;
    double *confidence_data = nullptr;
    double *strength_data = nullptr;
    double *amplitude_data = nullptr;
    double *phase_data = nullptr;
    int64_t *iteration_data = nullptr;

    if (total_periods > 0) {
        auto &list_child = ListVector::GetEntry(periods_list);
        auto &struct_entries = StructVector::GetEntries(list_child);
        period_data = FlatVector::GetData<double>(*struct_entries[0]);
        confidence_data = FlatVector::GetData<double>(*struct_entries[1]);
        strength_data = FlatVector::GetData<double>(*struct_entries[2]);
        amplitude_data = FlatVector::GetData<double>(*struct_entries[3]);
        phase_data = FlatVector::GetData<double>(*struct_entries[4]);
        iteration_data = FlatVector::GetData<int64_t>(*struct_entries[5]);
    }

    auto n_periods_data = FlatVector::GetData<int64_t>(*children[1]);
    auto primary_period_data = FlatVector::GetData<double>(*children[2]);

    size_t current_offset = 0;
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (row_results[row_idx].is_null) {
            list_data[row_idx].offset = current_offset;
            list_data[row_idx].length = 0;
            continue;
        }

        auto &res = row_results[row_idx].result;

        // Set list entry
        list_data[row_idx].offset = current_offset;
        list_data[row_idx].length = res.n_periods;

        // Copy period data (only if we have data pointers)
        if (res.n_periods > 0 && period_data != nullptr) {
            for (size_t i = 0; i < res.n_periods; i++) {
                period_data[current_offset + i] = res.period_values[i];
                confidence_data[current_offset + i] = res.confidence_values[i];
                strength_data[current_offset + i] = res.strength_values[i];
                amplitude_data[current_offset + i] = res.amplitude_values[i];
                phase_data[current_offset + i] = res.phase_values[i];
                iteration_data[current_offset + i] = res.iteration_values[i];
            }
        }

        current_offset += res.n_periods;

        // Set scalar fields
        n_periods_data[row_idx] = res.n_periods;
        primary_period_data[row_idx] = res.primary_period;
        FlatVector::GetData<string_t>(*children[3])[row_idx] = StringVector::AddString(*children[3], res.method);

        // Free FFI result
        anofox_free_flat_multi_period_result(&row_results[row_idx].result);
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
        ExtractListAsDouble(values_vec, count, row_idx, values);

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
        ExtractListAsDouble(values_vec, count, row_idx, values);

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

    // Two-pass approach to avoid incremental Reserve calls which can cause memory issues
    struct RowResult {
        FlatMultiPeriodResult result;
        bool is_null;
    };

    vector<RowResult> row_results(count);
    size_t total_periods = 0;

    // Pass 1: Compute all FFI results
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        row_results[row_idx].is_null = false;
        memset(&row_results[row_idx].result, 0, sizeof(FlatMultiPeriodResult));

        if (FlatVector::IsNull(values_vec, row_idx)) {
            row_results[row_idx].is_null = true;
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, count, row_idx, values);

        AnofoxError error;
        bool success = anofox_ts_detect_multiple_periods_flat(
            values.data(),
            values.size(),
            max_periods,
            min_confidence,
            min_strength,
            &row_results[row_idx].result,
            &error
        );

        if (!success) {
            // Clean up any already-allocated results
            for (idx_t i = 0; i < row_idx; i++) {
                if (!row_results[i].is_null) {
                    anofox_free_flat_multi_period_result(&row_results[i].result);
                }
            }
            throw InvalidInputException("ts_detect_multiple_periods failed: %s", error.message);
        }

        total_periods += row_results[row_idx].result.n_periods;
    }

    // Pass 2: Reserve all space at once, then copy data
    // IMPORTANT: Get fresh references after each operation that might reallocate
    {
        auto &children_init = StructVector::GetEntries(result);
        auto &periods_list_init = *children_init[0];

        // Reserve all space at once (reserve at least 1 to ensure valid list structure)
        ListVector::Reserve(periods_list_init, total_periods > 0 ? total_periods : 1);
        ListVector::SetListSize(periods_list_init, total_periods);
    }

    // Get fresh references after Reserve (Reserve may have reallocated memory)
    auto &children = StructVector::GetEntries(result);
    auto &periods_list = *children[0];

    // Get list_data pointer for the list entries
    auto list_data = FlatVector::GetData<list_entry_t>(periods_list);

    // Only get struct child pointers if we have periods to copy
    double *period_data = nullptr;
    double *confidence_data = nullptr;
    double *strength_data = nullptr;
    double *amplitude_data = nullptr;
    double *phase_data = nullptr;
    int64_t *iteration_data = nullptr;

    if (total_periods > 0) {
        auto &list_child = ListVector::GetEntry(periods_list);
        auto &struct_entries = StructVector::GetEntries(list_child);
        period_data = FlatVector::GetData<double>(*struct_entries[0]);
        confidence_data = FlatVector::GetData<double>(*struct_entries[1]);
        strength_data = FlatVector::GetData<double>(*struct_entries[2]);
        amplitude_data = FlatVector::GetData<double>(*struct_entries[3]);
        phase_data = FlatVector::GetData<double>(*struct_entries[4]);
        iteration_data = FlatVector::GetData<int64_t>(*struct_entries[5]);
    }

    auto n_periods_data = FlatVector::GetData<int64_t>(*children[1]);
    auto primary_period_data = FlatVector::GetData<double>(*children[2]);

    size_t current_offset = 0;
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (row_results[row_idx].is_null) {
            list_data[row_idx].offset = current_offset;
            list_data[row_idx].length = 0;
            continue;
        }

        auto &res = row_results[row_idx].result;

        // Set list entry
        list_data[row_idx].offset = current_offset;
        list_data[row_idx].length = res.n_periods;

        // Copy period data (only if we have data pointers)
        if (res.n_periods > 0 && period_data != nullptr) {
            for (size_t i = 0; i < res.n_periods; i++) {
                period_data[current_offset + i] = res.period_values[i];
                confidence_data[current_offset + i] = res.confidence_values[i];
                strength_data[current_offset + i] = res.strength_values[i];
                amplitude_data[current_offset + i] = res.amplitude_values[i];
                phase_data[current_offset + i] = res.phase_values[i];
                iteration_data[current_offset + i] = res.iteration_values[i];
            }
        }

        current_offset += res.n_periods;

        // Set scalar fields
        n_periods_data[row_idx] = res.n_periods;
        primary_period_data[row_idx] = res.primary_period;
        FlatVector::GetData<string_t>(*children[3])[row_idx] = StringVector::AddString(*children[3], res.method);

        // Free FFI result
        anofox_free_flat_multi_period_result(&row_results[row_idx].result);
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
