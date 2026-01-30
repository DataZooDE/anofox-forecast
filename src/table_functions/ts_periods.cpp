#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

namespace duckdb {

// Helper to check if a value is null using UnifiedVectorFormat
// This handles all vector types (flat, constant, dictionary)
static bool IsValueNull(Vector &vec, idx_t count, idx_t row_idx) {
    UnifiedVectorFormat format;
    vec.ToUnifiedFormat(count, format);
    auto idx = format.sel->get_index(row_idx);
    return !format.validity.RowIsValid(idx);
}

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
    period_children.push_back(make_pair("period", LogicalType(LogicalTypeId::DOUBLE)));
    period_children.push_back(make_pair("confidence", LogicalType(LogicalTypeId::DOUBLE)));
    period_children.push_back(make_pair("strength", LogicalType(LogicalTypeId::DOUBLE)));
    period_children.push_back(make_pair("amplitude", LogicalType(LogicalTypeId::DOUBLE)));
    period_children.push_back(make_pair("phase", LogicalType(LogicalTypeId::DOUBLE)));
    period_children.push_back(make_pair("iteration", LogicalType(LogicalTypeId::BIGINT)));
    period_children.push_back(make_pair("matches_expected", LogicalType(LogicalTypeId::BOOLEAN)));
    period_children.push_back(make_pair("matched_expected_period", LogicalType(LogicalTypeId::DOUBLE)));
    period_children.push_back(make_pair("match_deviation", LogicalType(LogicalTypeId::DOUBLE)));
    auto period_type = LogicalType::STRUCT(std::move(period_children));

    // Outer result struct
    child_list_t<LogicalType> children;
    children.push_back(make_pair("periods", LogicalType::LIST(period_type)));
    children.push_back(make_pair("n_periods", LogicalType(LogicalTypeId::BIGINT)));
    children.push_back(make_pair("primary_period", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    return LogicalType::STRUCT(std::move(children));
}

// Default max_period for daily data (365 days catches weekly, monthly, quarterly patterns)
static const size_t DEFAULT_MAX_PERIOD = 365;

static void TsDetectPeriodsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto &method_vec = args.data[1];
    idx_t count = args.size();

    // Optional max_period parameter (default 0 = use Rust default of 365)
    size_t max_period = 0;
    if (args.ColumnCount() > 2 && !IsValueNull(args.data[2], count, 0)) {
        UnifiedVectorFormat max_period_format;
        args.data[2].ToUnifiedFormat(count, max_period_format);
        auto max_period_data = UnifiedVectorFormat::GetData<int64_t>(max_period_format);
        max_period = static_cast<size_t>(max_period_data[0]);
    }

    // Optional min_confidence parameter (default -1.0 = use method-specific default)
    // Use 0.0 to disable filtering, positive value for custom threshold
    double min_confidence = -1.0;
    if (args.ColumnCount() > 3 && !IsValueNull(args.data[3], count, 0)) {
        UnifiedVectorFormat min_conf_format;
        args.data[3].ToUnifiedFormat(count, min_conf_format);
        auto min_conf_data = UnifiedVectorFormat::GetData<double>(min_conf_format);
        min_confidence = min_conf_data[0];
    }

    // Optional expected_periods parameter (LIST of DOUBLE)
    vector<double> expected_periods;
    if (args.ColumnCount() > 4 && !IsValueNull(args.data[4], count, 0)) {
        ExtractListAsDouble(args.data[4], count, 0, expected_periods);
    }

    // Optional tolerance parameter (default -1.0 = use Rust default of 0.1)
    double tolerance = -1.0;
    if (args.ColumnCount() > 5 && !IsValueNull(args.data[5], count, 0)) {
        UnifiedVectorFormat tol_format;
        args.data[5].ToUnifiedFormat(count, tol_format);
        auto tol_data = UnifiedVectorFormat::GetData<double>(tol_format);
        tolerance = tol_data[0];
    }

    result.Flatten(count);

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

        if (IsValueNull(values_vec, count, row_idx)) {
            row_results[row_idx].is_null = true;
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, count, row_idx, values);

        const char *method_str = nullptr;
        if (!IsValueNull(method_vec, count, row_idx)) {
            UnifiedVectorFormat method_format;
            method_vec.ToUnifiedFormat(count, method_format);
            auto method_data = UnifiedVectorFormat::GetData<string_t>(method_format);
            auto method_idx = method_format.sel->get_index(row_idx);
            method_str = method_data[method_idx].GetData();
        }

        AnofoxError error;
        bool success = anofox_ts_detect_periods_flat(
            values.data(),
            values.size(),
            method_str,
            max_period,
            min_confidence,
            expected_periods.empty() ? nullptr : expected_periods.data(),
            expected_periods.size(),
            tolerance,
            &row_results[row_idx].result,
            &error
        );

        if (!success) {
            row_results[row_idx].is_null = true;
            FlatVector::SetNull(result, row_idx, true);
            continue;
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
    bool *matches_expected_data = nullptr;
    double *matched_expected_data = nullptr;
    double *match_deviation_data = nullptr;

    if (total_periods > 0) {
        auto &list_child = ListVector::GetEntry(periods_list);
        auto &struct_entries = StructVector::GetEntries(list_child);
        period_data = FlatVector::GetData<double>(*struct_entries[0]);
        confidence_data = FlatVector::GetData<double>(*struct_entries[1]);
        strength_data = FlatVector::GetData<double>(*struct_entries[2]);
        amplitude_data = FlatVector::GetData<double>(*struct_entries[3]);
        phase_data = FlatVector::GetData<double>(*struct_entries[4]);
        iteration_data = FlatVector::GetData<int64_t>(*struct_entries[5]);
        matches_expected_data = FlatVector::GetData<bool>(*struct_entries[6]);
        matched_expected_data = FlatVector::GetData<double>(*struct_entries[7]);
        match_deviation_data = FlatVector::GetData<double>(*struct_entries[8]);
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
                matches_expected_data[current_offset + i] = res.matches_expected_values[i];
                matched_expected_data[current_offset + i] = res.matched_expected_values[i];
                match_deviation_data[current_offset + i] = res.match_deviation_values[i];
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

    result.Flatten(count);

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

        if (IsValueNull(values_vec, count, row_idx)) {
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
            0,        // default max_period (use Rust default of 365)
            -1.0,     // default min_confidence (use method-specific default)
            nullptr,  // no expected_periods validation
            0,        // n_expected = 0
            -1.0,     // default tolerance
            &row_results[row_idx].result,
            &error
        );

        if (!success) {
            row_results[row_idx].is_null = true;
            FlatVector::SetNull(result, row_idx, true);
            continue;
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
    bool *matches_expected_data = nullptr;
    double *matched_expected_data = nullptr;
    double *match_deviation_data = nullptr;

    if (total_periods > 0) {
        auto &list_child = ListVector::GetEntry(periods_list);
        auto &struct_entries = StructVector::GetEntries(list_child);
        period_data = FlatVector::GetData<double>(*struct_entries[0]);
        confidence_data = FlatVector::GetData<double>(*struct_entries[1]);
        strength_data = FlatVector::GetData<double>(*struct_entries[2]);
        amplitude_data = FlatVector::GetData<double>(*struct_entries[3]);
        phase_data = FlatVector::GetData<double>(*struct_entries[4]);
        iteration_data = FlatVector::GetData<int64_t>(*struct_entries[5]);
        matches_expected_data = FlatVector::GetData<bool>(*struct_entries[6]);
        matched_expected_data = FlatVector::GetData<double>(*struct_entries[7]);
        match_deviation_data = FlatVector::GetData<double>(*struct_entries[8]);
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
                matches_expected_data[current_offset + i] = res.matches_expected_values[i];
                matched_expected_data[current_offset + i] = res.matched_expected_values[i];
                match_deviation_data[current_offset + i] = res.match_deviation_values[i];
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
    // Internal scalar function used by ts_detect_periods table macro
    // Named with underscore prefix to match API pattern (_ts_stats, etc.)
    ScalarFunctionSet ts_periods_set("_ts_detect_periods");

    // Single-argument version (values only, default method)
    auto simple_func = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetMultiPeriodResultType(),
        TsDetectPeriodsSimpleFunction
    );
    simple_func.stability = FunctionStability::VOLATILE;
    ts_periods_set.AddFunction(simple_func);

    // Two-argument version (values, method)
    auto method_func = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::VARCHAR)},
        GetMultiPeriodResultType(),
        TsDetectPeriodsFunction
    );
    method_func.stability = FunctionStability::VOLATILE;
    ts_periods_set.AddFunction(method_func);

    // Three-argument version (values, method, max_period)
    auto full_func = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::BIGINT)},
        GetMultiPeriodResultType(),
        TsDetectPeriodsFunction
    );
    full_func.stability = FunctionStability::VOLATILE;
    ts_periods_set.AddFunction(full_func);

    // Four-argument version (values, method, max_period, min_confidence)
    auto full_func_conf = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::BIGINT), LogicalType(LogicalTypeId::DOUBLE)},
        GetMultiPeriodResultType(),
        TsDetectPeriodsFunction
    );
    full_func_conf.stability = FunctionStability::VOLATILE;
    ts_periods_set.AddFunction(full_func_conf);

    // Five-argument version (values, method, max_period, min_confidence, expected_periods)
    auto func5 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::BIGINT), LogicalType(LogicalTypeId::DOUBLE), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetMultiPeriodResultType(),
        TsDetectPeriodsFunction
    );
    func5.stability = FunctionStability::VOLATILE;
    ts_periods_set.AddFunction(func5);

    // Six-argument version (values, method, max_period, min_confidence, expected_periods, tolerance)
    auto func6 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::BIGINT), LogicalType(LogicalTypeId::DOUBLE), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        GetMultiPeriodResultType(),
        TsDetectPeriodsFunction
    );
    func6.stability = FunctionStability::VOLATILE;
    ts_periods_set.AddFunction(func6);

    // Mark as internal to hide from duckdb_functions() and deprioritize in autocomplete
    CreateScalarFunctionInfo info(ts_periods_set);
    info.internal = true;
    loader.RegisterFunction(info);
}

// ============================================================================
// ts_estimate_period_fft - Estimate single period using FFT
// Returns: STRUCT(period, frequency, power, confidence, method)
// ============================================================================

static LogicalType GetSinglePeriodResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("period", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("frequency", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("power", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("confidence", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    return LogicalType::STRUCT(std::move(children));
}

static void TsEstimatePeriodFftFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    result.Flatten(count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (IsValueNull(values_vec, count, row_idx)) {
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
            FlatVector::SetNull(result, row_idx, true);
            continue;
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
    auto fft_func = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetSinglePeriodResultType(),
        TsEstimatePeriodFftFunction
    );
    // Disable constant folding for this function - struct returns don't work well with it
    fft_func.stability = FunctionStability::VOLATILE;
    ts_period_fft_set.AddFunction(fft_func);
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

    result.Flatten(count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (IsValueNull(values_vec, count, row_idx)) {
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
            FlatVector::SetNull(result, row_idx, true);
            continue;
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
    auto acf_func1 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetSinglePeriodResultType(),
        TsEstimatePeriodAcfFunction
    );
    acf_func1.stability = FunctionStability::VOLATILE;
    ts_period_acf_set.AddFunction(acf_func1);
    // Two-argument version with max_lag
    auto acf_func2 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::INTEGER)},
        GetSinglePeriodResultType(),
        TsEstimatePeriodAcfFunction
    );
    acf_func2.stability = FunctionStability::VOLATILE;
    ts_period_acf_set.AddFunction(acf_func2);
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

    result.Flatten(count);

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

        if (IsValueNull(values_vec, count, row_idx)) {
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
            row_results[row_idx].is_null = true;
            FlatVector::SetNull(result, row_idx, true);
            continue;
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
    bool *matches_expected_data = nullptr;
    double *matched_expected_data = nullptr;
    double *match_deviation_data = nullptr;

    if (total_periods > 0) {
        auto &list_child = ListVector::GetEntry(periods_list);
        auto &struct_entries = StructVector::GetEntries(list_child);
        period_data = FlatVector::GetData<double>(*struct_entries[0]);
        confidence_data = FlatVector::GetData<double>(*struct_entries[1]);
        strength_data = FlatVector::GetData<double>(*struct_entries[2]);
        amplitude_data = FlatVector::GetData<double>(*struct_entries[3]);
        phase_data = FlatVector::GetData<double>(*struct_entries[4]);
        iteration_data = FlatVector::GetData<int64_t>(*struct_entries[5]);
        matches_expected_data = FlatVector::GetData<bool>(*struct_entries[6]);
        matched_expected_data = FlatVector::GetData<double>(*struct_entries[7]);
        match_deviation_data = FlatVector::GetData<double>(*struct_entries[8]);
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
                matches_expected_data[current_offset + i] = res.matches_expected_values[i];
                matched_expected_data[current_offset + i] = res.matched_expected_values[i];
                match_deviation_data[current_offset + i] = res.match_deviation_values[i];
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
    auto func1 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetMultiPeriodResultType(),
        TsDetectMultiplePeriodsFunction
    );
    func1.stability = FunctionStability::VOLATILE;
    ts_multi_periods_set.AddFunction(func1);
    // With max_periods
    auto func2 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::INTEGER)},
        GetMultiPeriodResultType(),
        TsDetectMultiplePeriodsFunction
    );
    func2.stability = FunctionStability::VOLATILE;
    ts_multi_periods_set.AddFunction(func2);
    // With max_periods, min_confidence
    auto func3 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::INTEGER), LogicalType(LogicalTypeId::DOUBLE)},
        GetMultiPeriodResultType(),
        TsDetectMultiplePeriodsFunction
    );
    func3.stability = FunctionStability::VOLATILE;
    ts_multi_periods_set.AddFunction(func3);
    // With max_periods, min_confidence, min_strength
    auto func4 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::INTEGER), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE)},
        GetMultiPeriodResultType(),
        TsDetectMultiplePeriodsFunction
    );
    func4.stability = FunctionStability::VOLATILE;
    ts_multi_periods_set.AddFunction(func4);
    loader.RegisterFunction(ts_multi_periods_set);
}

// ============================================================================
// ts_autoperiod - FFT period detection with ACF validation
// Returns: STRUCT(period, fft_confidence, acf_validation, detected, method)
// ============================================================================

static LogicalType GetAutoperiodResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("period", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("fft_confidence", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("acf_validation", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("detected", LogicalType(LogicalTypeId::BOOLEAN)));
    children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    return LogicalType::STRUCT(std::move(children));
}

static void TsAutoperiodFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    // Optional acf_threshold parameter (default 0.0 = use default)
    double acf_threshold = 0.0;
    if (args.ColumnCount() > 1 && !FlatVector::IsNull(args.data[1], 0)) {
        acf_threshold = FlatVector::GetData<double>(args.data[1])[0];
    }

    result.Flatten(count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (IsValueNull(values_vec, count, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, count, row_idx, values);

        AutoperiodResultFFI ap_result;
        memset(&ap_result, 0, sizeof(ap_result));
        AnofoxError error;

        bool success = anofox_ts_autoperiod(
            values.data(),
            values.size(),
            acf_threshold,
            &ap_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);
        FlatVector::GetData<double>(*children[0])[row_idx] = ap_result.period;
        FlatVector::GetData<double>(*children[1])[row_idx] = ap_result.fft_confidence;
        FlatVector::GetData<double>(*children[2])[row_idx] = ap_result.acf_validation;
        FlatVector::GetData<bool>(*children[3])[row_idx] = ap_result.detected;
        FlatVector::GetData<string_t>(*children[4])[row_idx] = StringVector::AddString(*children[4], ap_result.method);
    }
}

void RegisterTsAutoperiodFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_autoperiod_set("ts_autoperiod");
    // Single-argument version (uses default threshold)
    auto func1 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetAutoperiodResultType(),
        TsAutoperiodFunction
    );
    func1.stability = FunctionStability::VOLATILE;
    ts_autoperiod_set.AddFunction(func1);
    // With explicit acf_threshold
    auto func2 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        GetAutoperiodResultType(),
        TsAutoperiodFunction
    );
    func2.stability = FunctionStability::VOLATILE;
    ts_autoperiod_set.AddFunction(func2);
    loader.RegisterFunction(ts_autoperiod_set);
}

// ============================================================================
// ts_cfd_autoperiod - First-differenced FFT with ACF validation
// Returns: STRUCT(period, fft_confidence, acf_validation, detected, method)
// ============================================================================

static void TsCfdAutoperiodFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    // Optional acf_threshold parameter (default 0.0 = use default)
    double acf_threshold = 0.0;
    if (args.ColumnCount() > 1 && !FlatVector::IsNull(args.data[1], 0)) {
        acf_threshold = FlatVector::GetData<double>(args.data[1])[0];
    }

    result.Flatten(count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (IsValueNull(values_vec, count, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, count, row_idx, values);

        AutoperiodResultFFI ap_result;
        memset(&ap_result, 0, sizeof(ap_result));
        AnofoxError error;

        bool success = anofox_ts_cfd_autoperiod(
            values.data(),
            values.size(),
            acf_threshold,
            &ap_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);
        FlatVector::GetData<double>(*children[0])[row_idx] = ap_result.period;
        FlatVector::GetData<double>(*children[1])[row_idx] = ap_result.fft_confidence;
        FlatVector::GetData<double>(*children[2])[row_idx] = ap_result.acf_validation;
        FlatVector::GetData<bool>(*children[3])[row_idx] = ap_result.detected;
        FlatVector::GetData<string_t>(*children[4])[row_idx] = StringVector::AddString(*children[4], ap_result.method);
    }
}

void RegisterTsCfdAutoperiodFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_cfd_autoperiod_set("ts_cfd_autoperiod");
    // Single-argument version (uses default threshold)
    auto func1 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetAutoperiodResultType(),
        TsCfdAutoperiodFunction
    );
    func1.stability = FunctionStability::VOLATILE;
    ts_cfd_autoperiod_set.AddFunction(func1);
    // With explicit acf_threshold
    auto func2 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        GetAutoperiodResultType(),
        TsCfdAutoperiodFunction
    );
    func2.stability = FunctionStability::VOLATILE;
    ts_cfd_autoperiod_set.AddFunction(func2);
    loader.RegisterFunction(ts_cfd_autoperiod_set);
}

// ============================================================================
// Lomb-Scargle Periodogram
// ============================================================================

static LogicalType GetLombScargleResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("period", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("frequency", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("power", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("false_alarm_prob", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    return LogicalType::STRUCT(std::move(children));
}

static void TsLombScargleFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto count = args.size();

    // Handle optional parameters
    double min_period = 0.0;  // 0 means use default
    double max_period = 0.0;
    size_t n_frequencies = 0;

    if (args.ColumnCount() >= 2 && !FlatVector::IsNull(args.data[1], 0)) {
        min_period = FlatVector::GetData<double>(args.data[1])[0];
    }
    if (args.ColumnCount() >= 3 && !FlatVector::IsNull(args.data[2], 0)) {
        max_period = FlatVector::GetData<double>(args.data[2])[0];
    }
    if (args.ColumnCount() >= 4 && !FlatVector::IsNull(args.data[3], 0)) {
        n_frequencies = static_cast<size_t>(FlatVector::GetData<int64_t>(args.data[3])[0]);
    }

    result.Flatten(count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (IsValueNull(values_vec, count, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, count, row_idx, values);

        if (values.size() < 4) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        LombScargleResultFFI ls_result;
        memset(&ls_result, 0, sizeof(ls_result));
        AnofoxError error;

        bool success = anofox_ts_lomb_scargle(
            values.data(),
            values.size(),
            min_period,
            max_period,
            n_frequencies,
            &ls_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);
        FlatVector::GetData<double>(*children[0])[row_idx] = ls_result.period;
        FlatVector::GetData<double>(*children[1])[row_idx] = ls_result.frequency;
        FlatVector::GetData<double>(*children[2])[row_idx] = ls_result.power;
        FlatVector::GetData<double>(*children[3])[row_idx] = ls_result.false_alarm_prob;
        FlatVector::GetData<string_t>(*children[4])[row_idx] = StringVector::AddString(*children[4], ls_result.method);
    }
}

void RegisterTsLombScargleFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_lomb_scargle_set("ts_lomb_scargle");
    // Single-argument version (uses defaults)
    auto func1 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetLombScargleResultType(),
        TsLombScargleFunction
    );
    func1.stability = FunctionStability::VOLATILE;
    ts_lomb_scargle_set.AddFunction(func1);
    // With min_period
    auto func2 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        GetLombScargleResultType(),
        TsLombScargleFunction
    );
    func2.stability = FunctionStability::VOLATILE;
    ts_lomb_scargle_set.AddFunction(func2);
    // With min_period and max_period
    auto func3 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE)},
        GetLombScargleResultType(),
        TsLombScargleFunction
    );
    func3.stability = FunctionStability::VOLATILE;
    ts_lomb_scargle_set.AddFunction(func3);
    // With min_period, max_period, and n_frequencies
    auto func4 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::BIGINT)},
        GetLombScargleResultType(),
        TsLombScargleFunction
    );
    func4.stability = FunctionStability::VOLATILE;
    ts_lomb_scargle_set.AddFunction(func4);
    loader.RegisterFunction(ts_lomb_scargle_set);
}

// ============================================================================
// AIC Period Comparison
// ============================================================================

static LogicalType GetAicPeriodResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("period", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("aic", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("bic", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("rss", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("r_squared", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    return LogicalType::STRUCT(std::move(children));
}

static void TsAicPeriodFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto count = args.size();

    double min_period = 0.0;
    double max_period = 0.0;
    size_t n_candidates = 0;

    if (args.ColumnCount() >= 2 && !FlatVector::IsNull(args.data[1], 0)) {
        min_period = FlatVector::GetData<double>(args.data[1])[0];
    }
    if (args.ColumnCount() >= 3 && !FlatVector::IsNull(args.data[2], 0)) {
        max_period = FlatVector::GetData<double>(args.data[2])[0];
    }
    if (args.ColumnCount() >= 4 && !FlatVector::IsNull(args.data[3], 0)) {
        n_candidates = static_cast<size_t>(FlatVector::GetData<int64_t>(args.data[3])[0]);
    }

    result.Flatten(count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (IsValueNull(values_vec, count, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, count, row_idx, values);

        if (values.size() < 8) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        AicPeriodResultFFI aic_result;
        memset(&aic_result, 0, sizeof(aic_result));
        AnofoxError error;

        bool success = anofox_ts_aic_period(
            values.data(),
            values.size(),
            min_period,
            max_period,
            n_candidates,
            &aic_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);
        FlatVector::GetData<double>(*children[0])[row_idx] = aic_result.period;
        FlatVector::GetData<double>(*children[1])[row_idx] = aic_result.aic;
        FlatVector::GetData<double>(*children[2])[row_idx] = aic_result.bic;
        FlatVector::GetData<double>(*children[3])[row_idx] = aic_result.rss;
        FlatVector::GetData<double>(*children[4])[row_idx] = aic_result.r_squared;
        FlatVector::GetData<string_t>(*children[5])[row_idx] = StringVector::AddString(*children[5], aic_result.method);
    }
}

void RegisterTsAicPeriodFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_aic_period_set("ts_aic_period");
    auto func1 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetAicPeriodResultType(),
        TsAicPeriodFunction
    );
    func1.stability = FunctionStability::VOLATILE;
    ts_aic_period_set.AddFunction(func1);
    auto func2 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        GetAicPeriodResultType(),
        TsAicPeriodFunction
    );
    func2.stability = FunctionStability::VOLATILE;
    ts_aic_period_set.AddFunction(func2);
    auto func3 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE)},
        GetAicPeriodResultType(),
        TsAicPeriodFunction
    );
    func3.stability = FunctionStability::VOLATILE;
    ts_aic_period_set.AddFunction(func3);
    auto func4 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::BIGINT)},
        GetAicPeriodResultType(),
        TsAicPeriodFunction
    );
    func4.stability = FunctionStability::VOLATILE;
    ts_aic_period_set.AddFunction(func4);
    loader.RegisterFunction(ts_aic_period_set);
}

// ============================================================================
// SSA Period Detection
// ============================================================================

static LogicalType GetSsaPeriodResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("period", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("variance_explained", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("n_eigenvalues", LogicalType(LogicalTypeId::BIGINT)));
    children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    return LogicalType::STRUCT(std::move(children));
}

static void TsSsaPeriodFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto count = args.size();

    size_t window_size = 0;
    size_t n_components = 0;

    if (args.ColumnCount() >= 2 && !FlatVector::IsNull(args.data[1], 0)) {
        window_size = static_cast<size_t>(FlatVector::GetData<int64_t>(args.data[1])[0]);
    }
    if (args.ColumnCount() >= 3 && !FlatVector::IsNull(args.data[2], 0)) {
        n_components = static_cast<size_t>(FlatVector::GetData<int64_t>(args.data[2])[0]);
    }

    result.Flatten(count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (IsValueNull(values_vec, count, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, count, row_idx, values);

        if (values.size() < 16) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        SsaPeriodResultFFI ssa_result;
        memset(&ssa_result, 0, sizeof(ssa_result));
        AnofoxError error;

        bool success = anofox_ts_ssa_period(
            values.data(),
            values.size(),
            window_size,
            n_components,
            &ssa_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);
        FlatVector::GetData<double>(*children[0])[row_idx] = ssa_result.period;
        FlatVector::GetData<double>(*children[1])[row_idx] = ssa_result.variance_explained;
        FlatVector::GetData<int64_t>(*children[2])[row_idx] = static_cast<int64_t>(ssa_result.n_eigenvalues);
        FlatVector::GetData<string_t>(*children[3])[row_idx] = StringVector::AddString(*children[3], ssa_result.method);
    }
}

void RegisterTsSsaPeriodFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_ssa_period_set("ts_ssa_period");
    auto func1 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetSsaPeriodResultType(),
        TsSsaPeriodFunction
    );
    func1.stability = FunctionStability::VOLATILE;
    ts_ssa_period_set.AddFunction(func1);
    auto func2 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT)},
        GetSsaPeriodResultType(),
        TsSsaPeriodFunction
    );
    func2.stability = FunctionStability::VOLATILE;
    ts_ssa_period_set.AddFunction(func2);
    auto func3 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT), LogicalType(LogicalTypeId::BIGINT)},
        GetSsaPeriodResultType(),
        TsSsaPeriodFunction
    );
    func3.stability = FunctionStability::VOLATILE;
    ts_ssa_period_set.AddFunction(func3);
    loader.RegisterFunction(ts_ssa_period_set);
}

// =============================================================================
// STL Period Detection
// =============================================================================

static LogicalType GetStlPeriodResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("period", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("seasonal_strength", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("trend_strength", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    return LogicalType::STRUCT(std::move(children));
}

static void TsStlPeriodFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto count = args.size();

    size_t min_period = 0;
    size_t max_period = 0;
    size_t n_candidates = 0;

    if (args.ColumnCount() >= 2 && !FlatVector::IsNull(args.data[1], 0)) {
        min_period = static_cast<size_t>(FlatVector::GetData<int64_t>(args.data[1])[0]);
    }
    if (args.ColumnCount() >= 3 && !FlatVector::IsNull(args.data[2], 0)) {
        max_period = static_cast<size_t>(FlatVector::GetData<int64_t>(args.data[2])[0]);
    }
    if (args.ColumnCount() >= 4 && !FlatVector::IsNull(args.data[3], 0)) {
        n_candidates = static_cast<size_t>(FlatVector::GetData<int64_t>(args.data[3])[0]);
    }

    result.Flatten(count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (IsValueNull(values_vec, count, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, count, row_idx, values);

        if (values.size() < 16) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        StlPeriodResultFFI stl_result;
        memset(&stl_result, 0, sizeof(stl_result));
        AnofoxError error;

        bool success = anofox_ts_stl_period(
            values.data(),
            values.size(),
            min_period,
            max_period,
            n_candidates,
            &stl_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);
        FlatVector::GetData<double>(*children[0])[row_idx] = stl_result.period;
        FlatVector::GetData<double>(*children[1])[row_idx] = stl_result.seasonal_strength;
        FlatVector::GetData<double>(*children[2])[row_idx] = stl_result.trend_strength;
        FlatVector::GetData<string_t>(*children[3])[row_idx] = StringVector::AddString(*children[3], stl_result.method);
    }
}

void RegisterTsStlPeriodFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_stl_period_set("ts_stl_period");
    auto func1 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetStlPeriodResultType(),
        TsStlPeriodFunction
    );
    func1.stability = FunctionStability::VOLATILE;
    ts_stl_period_set.AddFunction(func1);
    auto func2 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT)},
        GetStlPeriodResultType(),
        TsStlPeriodFunction
    );
    func2.stability = FunctionStability::VOLATILE;
    ts_stl_period_set.AddFunction(func2);
    auto func3 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT), LogicalType(LogicalTypeId::BIGINT)},
        GetStlPeriodResultType(),
        TsStlPeriodFunction
    );
    func3.stability = FunctionStability::VOLATILE;
    ts_stl_period_set.AddFunction(func3);
    auto func4 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT), LogicalType(LogicalTypeId::BIGINT), LogicalType(LogicalTypeId::BIGINT)},
        GetStlPeriodResultType(),
        TsStlPeriodFunction
    );
    func4.stability = FunctionStability::VOLATILE;
    ts_stl_period_set.AddFunction(func4);
    loader.RegisterFunction(ts_stl_period_set);
}

// =============================================================================
// Matrix Profile Period Detection
// =============================================================================

static LogicalType GetMatrixProfilePeriodResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("period", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("confidence", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("n_motifs", LogicalType(LogicalTypeId::BIGINT)));
    children.push_back(make_pair("subsequence_length", LogicalType(LogicalTypeId::BIGINT)));
    children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    return LogicalType::STRUCT(std::move(children));
}

static void TsMatrixProfilePeriodFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto count = args.size();

    size_t subsequence_length = 0;
    size_t n_best = 0;

    if (args.ColumnCount() >= 2 && !FlatVector::IsNull(args.data[1], 0)) {
        subsequence_length = static_cast<size_t>(FlatVector::GetData<int64_t>(args.data[1])[0]);
    }
    if (args.ColumnCount() >= 3 && !FlatVector::IsNull(args.data[2], 0)) {
        n_best = static_cast<size_t>(FlatVector::GetData<int64_t>(args.data[2])[0]);
    }

    result.Flatten(count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (IsValueNull(values_vec, count, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, count, row_idx, values);

        if (values.size() < 16) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        MatrixProfilePeriodResultFFI mp_result;
        memset(&mp_result, 0, sizeof(mp_result));
        AnofoxError error;

        bool success = anofox_ts_matrix_profile_period(
            values.data(),
            values.size(),
            subsequence_length,
            n_best,
            &mp_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);
        FlatVector::GetData<double>(*children[0])[row_idx] = mp_result.period;
        FlatVector::GetData<double>(*children[1])[row_idx] = mp_result.confidence;
        FlatVector::GetData<int64_t>(*children[2])[row_idx] = static_cast<int64_t>(mp_result.n_motifs);
        FlatVector::GetData<int64_t>(*children[3])[row_idx] = static_cast<int64_t>(mp_result.subsequence_length);
        FlatVector::GetData<string_t>(*children[4])[row_idx] = StringVector::AddString(*children[4], mp_result.method);
    }
}

void RegisterTsMatrixProfilePeriodFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_mp_period_set("ts_matrix_profile_period");
    auto func1 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetMatrixProfilePeriodResultType(),
        TsMatrixProfilePeriodFunction
    );
    func1.stability = FunctionStability::VOLATILE;
    ts_mp_period_set.AddFunction(func1);
    auto func2 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT)},
        GetMatrixProfilePeriodResultType(),
        TsMatrixProfilePeriodFunction
    );
    func2.stability = FunctionStability::VOLATILE;
    ts_mp_period_set.AddFunction(func2);
    auto func3 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT), LogicalType(LogicalTypeId::BIGINT)},
        GetMatrixProfilePeriodResultType(),
        TsMatrixProfilePeriodFunction
    );
    func3.stability = FunctionStability::VOLATILE;
    ts_mp_period_set.AddFunction(func3);
    loader.RegisterFunction(ts_mp_period_set);
}

// =============================================================================
// SAZED Period Detection
// =============================================================================

static LogicalType GetSazedPeriodResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("period", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("power", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("snr", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    return LogicalType::STRUCT(std::move(children));
}

static void TsSazedPeriodFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto count = args.size();

    size_t min_period = 0;
    size_t max_period = 0;
    size_t zero_pad_factor = 0;

    if (args.ColumnCount() >= 2 && !FlatVector::IsNull(args.data[1], 0)) {
        min_period = static_cast<size_t>(FlatVector::GetData<int64_t>(args.data[1])[0]);
    }
    if (args.ColumnCount() >= 3 && !FlatVector::IsNull(args.data[2], 0)) {
        max_period = static_cast<size_t>(FlatVector::GetData<int64_t>(args.data[2])[0]);
    }
    if (args.ColumnCount() >= 4 && !FlatVector::IsNull(args.data[3], 0)) {
        zero_pad_factor = static_cast<size_t>(FlatVector::GetData<int64_t>(args.data[3])[0]);
    }

    result.Flatten(count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (IsValueNull(values_vec, count, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, count, row_idx, values);

        if (values.size() < 8) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        SazedPeriodResultFFI sazed_result;
        memset(&sazed_result, 0, sizeof(sazed_result));
        AnofoxError error;

        bool success = anofox_ts_sazed_period(
            values.data(),
            values.size(),
            min_period,
            max_period,
            zero_pad_factor,
            &sazed_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);
        FlatVector::GetData<double>(*children[0])[row_idx] = sazed_result.period;
        FlatVector::GetData<double>(*children[1])[row_idx] = sazed_result.power;
        FlatVector::GetData<double>(*children[2])[row_idx] = sazed_result.snr;
        FlatVector::GetData<string_t>(*children[3])[row_idx] = StringVector::AddString(*children[3], sazed_result.method);
    }
}

void RegisterTsSazedPeriodFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_sazed_period_set("ts_sazed_period");
    auto func1 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetSazedPeriodResultType(),
        TsSazedPeriodFunction
    );
    func1.stability = FunctionStability::VOLATILE;
    ts_sazed_period_set.AddFunction(func1);
    auto func2 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT)},
        GetSazedPeriodResultType(),
        TsSazedPeriodFunction
    );
    func2.stability = FunctionStability::VOLATILE;
    ts_sazed_period_set.AddFunction(func2);
    auto func3 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT), LogicalType(LogicalTypeId::BIGINT)},
        GetSazedPeriodResultType(),
        TsSazedPeriodFunction
    );
    func3.stability = FunctionStability::VOLATILE;
    ts_sazed_period_set.AddFunction(func3);
    auto func4 = ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT), LogicalType(LogicalTypeId::BIGINT), LogicalType(LogicalTypeId::BIGINT)},
        GetSazedPeriodResultType(),
        TsSazedPeriodFunction
    );
    func4.stability = FunctionStability::VOLATILE;
    ts_sazed_period_set.AddFunction(func4);
    loader.RegisterFunction(ts_sazed_period_set);
}

} // namespace duckdb
