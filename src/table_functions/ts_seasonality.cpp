#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

// ============================================================================
// ts_detect_seasonality - Returns array of detected periods
// ============================================================================

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

static void TsDetectSeasonalityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(list_vec, row_idx, values);

        int *periods = nullptr;
        size_t n_periods = 0;
        AnofoxError error;

        bool success = anofox_ts_detect_seasonality(
            values.data(),
            values.size(),
            0,  // max_period = auto
            &periods,
            &n_periods,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Build the result list
        auto list_data = FlatVector::GetData<list_entry_t>(result);
        auto &list_child = ListVector::GetEntry(result);
        auto current_size = ListVector::GetListSize(result);

        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = n_periods;

        ListVector::Reserve(result, current_size + n_periods);
        ListVector::SetListSize(result, current_size + n_periods);

        auto child_data = FlatVector::GetData<int32_t>(list_child);
        for (size_t i = 0; i < n_periods; i++) {
            child_data[current_size + i] = periods[i];
        }

        if (periods) {
            anofox_free_int_array(periods);
        }
    }
}

void RegisterTsDetectSeasonalityFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_detect_set("ts_detect_seasonality");
    ts_detect_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::INTEGER),
        TsDetectSeasonalityFunction
    ));
    loader.RegisterFunction(ts_detect_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_detect_seasonality");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::INTEGER),
        TsDetectSeasonalityFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_analyze_seasonality - Returns detailed analysis STRUCT
// C++ API: ts_analyze_seasonality(timestamps[], values[]) → STRUCT
// Returns: STRUCT(detected_periods, primary_period, seasonal_strength, trend_strength)
// ============================================================================

static LogicalType GetSeasonalityResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("detected_periods", LogicalType::LIST(LogicalType::INTEGER)));
    children.push_back(make_pair("primary_period", LogicalType::INTEGER));
    children.push_back(make_pair("seasonal_strength", LogicalType::DOUBLE));
    children.push_back(make_pair("trend_strength", LogicalType::DOUBLE));
    return LogicalType::STRUCT(std::move(children));
}

// Version with timestamps (C++ API compatible)
static void TsAnalyzeSeasonalityWithTimestampsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &ts_vec = args.data[0];      // timestamps (ignored internally)
    auto &values_vec = args.data[1];  // values
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, row_idx, values);

        SeasonalityResult seas_result;
        memset(&seas_result, 0, sizeof(seas_result));
        AnofoxError error;

        bool success = anofox_ts_analyze_seasonality(
            nullptr,        // timestamps (ignored)
            0,              // timestamps_len
            values.data(),
            values.size(),
            0,              // max_period = auto
            &seas_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);

        // Set detected_periods list
        {
            auto &periods_list = *children[0];
            auto list_data = FlatVector::GetData<list_entry_t>(periods_list);
            auto &list_child = ListVector::GetEntry(periods_list);
            auto current_size = ListVector::GetListSize(periods_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = seas_result.n_periods;

            ListVector::Reserve(periods_list, current_size + seas_result.n_periods);
            ListVector::SetListSize(periods_list, current_size + seas_result.n_periods);

            auto child_data = FlatVector::GetData<int32_t>(list_child);
            for (size_t i = 0; i < seas_result.n_periods; i++) {
                child_data[current_size + i] = seas_result.detected_periods[i];
            }
        }

        // Set scalar fields
        FlatVector::GetData<int32_t>(*children[1])[row_idx] = seas_result.primary_period;
        FlatVector::GetData<double>(*children[2])[row_idx] = seas_result.seasonal_strength;
        FlatVector::GetData<double>(*children[3])[row_idx] = seas_result.trend_strength;

        anofox_free_seasonality_result(&seas_result);
    }
}

// Single-argument version (convenience wrapper)
static void TsAnalyzeSeasonalityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
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

        SeasonalityResult seas_result;
        memset(&seas_result, 0, sizeof(seas_result));
        AnofoxError error;

        bool success = anofox_ts_analyze_seasonality(
            nullptr,        // timestamps (ignored)
            0,              // timestamps_len
            values.data(),
            values.size(),
            0,              // max_period = auto
            &seas_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);

        // Set detected_periods list
        {
            auto &periods_list = *children[0];
            auto list_data = FlatVector::GetData<list_entry_t>(periods_list);
            auto &list_child = ListVector::GetEntry(periods_list);
            auto current_size = ListVector::GetListSize(periods_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = seas_result.n_periods;

            ListVector::Reserve(periods_list, current_size + seas_result.n_periods);
            ListVector::SetListSize(periods_list, current_size + seas_result.n_periods);

            auto child_data = FlatVector::GetData<int32_t>(list_child);
            for (size_t i = 0; i < seas_result.n_periods; i++) {
                child_data[current_size + i] = seas_result.detected_periods[i];
            }
        }

        // Set scalar fields
        FlatVector::GetData<int32_t>(*children[1])[row_idx] = seas_result.primary_period;
        FlatVector::GetData<double>(*children[2])[row_idx] = seas_result.seasonal_strength;
        FlatVector::GetData<double>(*children[3])[row_idx] = seas_result.trend_strength;

        anofox_free_seasonality_result(&seas_result);
    }
}

void RegisterTsAnalyzeSeasonalityFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_analyze_set("ts_analyze_seasonality");
    // Single-argument version (convenience)
    ts_analyze_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        GetSeasonalityResultType(),
        TsAnalyzeSeasonalityFunction
    ));
    // Two-argument version (C++ API compatible: timestamps, values)
    ts_analyze_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::TIMESTAMP), LogicalType::LIST(LogicalType::DOUBLE)},
        GetSeasonalityResultType(),
        TsAnalyzeSeasonalityWithTimestampsFunction
    ));
    loader.RegisterFunction(ts_analyze_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_analyze_seasonality");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        GetSeasonalityResultType(),
        TsAnalyzeSeasonalityFunction
    ));
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::TIMESTAMP), LogicalType::LIST(LogicalType::DOUBLE)},
        GetSeasonalityResultType(),
        TsAnalyzeSeasonalityWithTimestampsFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_classify_seasonality - Full seasonality classification with timing & modulation
// Returns: STRUCT(timing_classification, modulation_type, has_stable_timing,
//                 timing_variability, seasonal_strength, is_seasonal,
//                 cycle_strengths, weak_seasons)
// ============================================================================

static LogicalType GetSeasonalityClassificationResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("timing_classification", LogicalType::VARCHAR));
    children.push_back(make_pair("modulation_type", LogicalType::VARCHAR));
    children.push_back(make_pair("has_stable_timing", LogicalType::BOOLEAN));
    children.push_back(make_pair("timing_variability", LogicalType::DOUBLE));
    children.push_back(make_pair("seasonal_strength", LogicalType::DOUBLE));
    children.push_back(make_pair("is_seasonal", LogicalType::BOOLEAN));
    children.push_back(make_pair("cycle_strengths", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("weak_seasons", LogicalType::LIST(LogicalType::BIGINT)));
    return LogicalType::STRUCT(std::move(children));
}

// Helper to set list of doubles in result struct
static void SetDoubleListField(Vector &list_vec, idx_t row_idx, const double *data, size_t count) {
    auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
    auto &list_child = ListVector::GetEntry(list_vec);
    auto current_size = ListVector::GetListSize(list_vec);

    list_data[row_idx].offset = current_size;
    list_data[row_idx].length = count;

    ListVector::Reserve(list_vec, current_size + count);
    ListVector::SetListSize(list_vec, current_size + count);

    auto child_data = FlatVector::GetData<double>(list_child);
    for (size_t i = 0; i < count; i++) {
        child_data[current_size + i] = data[i];
    }
}

// Helper to set list of bigints in result struct
static void SetBigintListField(Vector &list_vec, idx_t row_idx, const size_t *data, size_t count) {
    auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
    auto &list_child = ListVector::GetEntry(list_vec);
    auto current_size = ListVector::GetListSize(list_vec);

    list_data[row_idx].offset = current_size;
    list_data[row_idx].length = count;

    ListVector::Reserve(list_vec, current_size + count);
    ListVector::SetListSize(list_vec, current_size + count);

    auto child_data = FlatVector::GetData<int64_t>(list_child);
    for (size_t i = 0; i < count; i++) {
        child_data[current_size + i] = static_cast<int64_t>(data[i]);
    }
}

// ts_classify_seasonality(values, period, [strength_threshold], [timing_threshold])
static void TsClassifySeasonalityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto &period_vec = args.data[1];
    idx_t count = args.size();

    // Get optional threshold parameters
    double strength_threshold = 0.3;  // default
    double timing_threshold = 0.1;    // default

    if (args.ColumnCount() > 2 && !FlatVector::IsNull(args.data[2], 0)) {
        strength_threshold = FlatVector::GetData<double>(args.data[2])[0];
    }
    if (args.ColumnCount() > 3 && !FlatVector::IsNull(args.data[3], 0)) {
        timing_threshold = FlatVector::GetData<double>(args.data[3])[0];
    }

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx) || FlatVector::IsNull(period_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, row_idx, values);

        double period = FlatVector::GetData<double>(period_vec)[row_idx];
        if (period <= 0 || values.size() < static_cast<size_t>(2 * period)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Call FFI for seasonality classification
        SeasonalityClassificationFFI class_result;
        memset(&class_result, 0, sizeof(class_result));
        AnofoxError error;

        bool success = anofox_ts_classify_seasonality(
            values.data(),
            values.size(),
            period,
            strength_threshold,
            timing_threshold,
            &class_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Call FFI for amplitude modulation detection
        AmplitudeModulationResultFFI mod_result;
        memset(&mod_result, 0, sizeof(mod_result));

        bool mod_success = anofox_ts_detect_amplitude_modulation(
            values.data(),
            values.size(),
            period,
            0.2,  // modulation_threshold (default)
            strength_threshold,  // use same seasonality threshold
            &mod_result,
            &error
        );

        // Get struct children
        auto &children = StructVector::GetEntries(result);

        // timing_classification (index 0)
        FlatVector::GetData<string_t>(*children[0])[row_idx] =
            StringVector::AddString(*children[0], class_result.classification);

        // modulation_type (index 1)
        if (mod_success) {
            FlatVector::GetData<string_t>(*children[1])[row_idx] =
                StringVector::AddString(*children[1], mod_result.modulation_type);
        } else {
            FlatVector::GetData<string_t>(*children[1])[row_idx] =
                StringVector::AddString(*children[1], "unknown");
        }

        // has_stable_timing (index 2)
        FlatVector::GetData<bool>(*children[2])[row_idx] = class_result.has_stable_timing;

        // timing_variability (index 3)
        FlatVector::GetData<double>(*children[3])[row_idx] = class_result.timing_variability;

        // seasonal_strength (index 4)
        FlatVector::GetData<double>(*children[4])[row_idx] = class_result.seasonal_strength;

        // is_seasonal (index 5)
        FlatVector::GetData<bool>(*children[5])[row_idx] = class_result.is_seasonal;

        // cycle_strengths (index 6)
        SetDoubleListField(*children[6], row_idx, class_result.cycle_strengths, class_result.n_cycle_strengths);

        // weak_seasons (index 7)
        SetBigintListField(*children[7], row_idx, class_result.weak_seasons, class_result.n_weak_seasons);

        // Free FFI results
        anofox_free_seasonality_classification_result(&class_result);
        if (mod_success) {
            anofox_free_amplitude_modulation_result(&mod_result);
        }
    }
}

void RegisterTsClassifySeasonalityFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_classify_set("ts_classify_seasonality");

    // ts_classify_seasonality(values, period) -> STRUCT
    ts_classify_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    ));

    // ts_classify_seasonality(values, period, strength_threshold) -> STRUCT
    ts_classify_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    ));

    // ts_classify_seasonality(values, period, strength_threshold, timing_threshold) -> STRUCT
    ts_classify_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    ));

    loader.RegisterFunction(ts_classify_set);

    // Also register with anofox_ prefix
    ScalarFunctionSet anofox_classify_set("anofox_fcst_ts_classify_seasonality");
    anofox_classify_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    ));
    anofox_classify_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    ));
    anofox_classify_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    ));
    loader.RegisterFunction(anofox_classify_set);
}

} // namespace duckdb
