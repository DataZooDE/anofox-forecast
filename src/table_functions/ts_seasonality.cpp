#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/types/vector.hpp"
#include "ts_list_marshal.hpp"
#include <cmath>

namespace duckdb {

// List extraction and scalar-parameter reads below go through
// ts_list_marshal.hpp's ListDoubleReader::ReadSingleSeries and
// ScalarArgReader<T> (Phase 12 D-01/D-03/D-04/T-12-07, plan 12-02): the series
// is NULL-free after the Rust single-series gate (trims leading/trailing
// NULLs, errors on an interior NULL naming its 1-based position), and every
// scalar parameter (period, thresholds) is read per row (constant-vector
// safe) instead of row 0.

// ============================================================================
// ts_detect_seasonality - Returns array of detected periods
// ============================================================================

static void TsDetectSeasonalityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    ListDoubleReader series_reader(list_vec, count);

    result.SetVectorType(VectorType::FLAT_VECTOR);

    vector<double> values;
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (!series_reader.ReadSingleSeries(row_idx, values, "ts_detect_seasonality")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

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
        auto list_data = FlatDataMut<list_entry_t>(result);
        auto &list_child = ListChildMut(result);
        auto current_size = ListVector::GetListSize(result);

        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = n_periods;

        ListVector::Reserve(result, current_size + n_periods);
        ListVector::SetListSize(result, current_size + n_periods);

        auto child_data = FlatDataMut<int32_t>(list_child);
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
    ts_detect_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType::LIST(LogicalType(LogicalTypeId::INTEGER)),
        TsDetectSeasonalityFunction
    )));
    {
        CreateScalarFunctionInfo info(ts_detect_set);
        FunctionDescription desc;
        desc.description = "Tests whether a time series exhibits statistically significant seasonality. Returns TRUE/FALSE or a confidence score.";
        desc.examples = {"ts_detect_seasonality(LIST(value ORDER BY date))", "ts_detect_seasonality(LIST(value ORDER BY date), 7)"};
        desc.categories = {"time-series", "seasonality"};
        desc.parameter_names = {"values"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_detect_seasonality");
    anofox_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType::LIST(LogicalType(LogicalTypeId::INTEGER)),
        TsDetectSeasonalityFunction
    )));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_detect_seasonality";
        FunctionDescription desc;
        desc.description = "Tests whether a time series exhibits statistically significant seasonality. Returns TRUE/FALSE or a confidence score.";
        desc.examples = {"ts_detect_seasonality(LIST(value ORDER BY date))"};
        desc.categories = {"time-series", "seasonality"};
        desc.parameter_names = {"values"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_analyze_seasonality - Returns detailed analysis STRUCT
// C++ API: ts_analyze_seasonality(timestamps[], values[]) → STRUCT
// Returns: STRUCT(detected_periods, primary_period, seasonal_strength, trend_strength)
// ============================================================================

static LogicalType GetSeasonalityResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("detected_periods", LogicalType::LIST(LogicalType(LogicalTypeId::INTEGER))));
    children.push_back(make_pair("primary_period", LogicalType(LogicalTypeId::INTEGER)));
    children.push_back(make_pair("seasonal_strength", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("trend_strength", LogicalType(LogicalTypeId::DOUBLE)));
    return LogicalType::STRUCT(std::move(children));
}

// Version with timestamps (C++ API compatible)
static void TsAnalyzeSeasonalityWithTimestampsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &ts_vec = args.data[0];      // timestamps (ignored internally; never read)
    auto &values_vec = args.data[1];  // values
    idx_t count = args.size();

    ListDoubleReader series_reader(values_vec, count);

    result.SetVectorType(VectorType::FLAT_VECTOR);

    vector<double> values;
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (!series_reader.ReadSingleSeries(row_idx, values, "ts_analyze_seasonality")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

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
            auto &periods_list = StructEntry(children[0]);
            auto list_data = FlatDataMut<list_entry_t>(periods_list);
            auto &list_child = ListChildMut(periods_list);
            auto current_size = ListVector::GetListSize(periods_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = seas_result.n_periods;

            ListVector::Reserve(periods_list, current_size + seas_result.n_periods);
            ListVector::SetListSize(periods_list, current_size + seas_result.n_periods);

            auto child_data = FlatDataMut<int32_t>(list_child);
            for (size_t i = 0; i < seas_result.n_periods; i++) {
                child_data[current_size + i] = seas_result.detected_periods[i];
            }
        }

        // Set scalar fields
        FlatDataMut<int32_t>(StructEntry(children[1]))[row_idx] = seas_result.primary_period;
        FlatDataMut<double>(StructEntry(children[2]))[row_idx] = seas_result.seasonal_strength;
        FlatDataMut<double>(StructEntry(children[3]))[row_idx] = seas_result.trend_strength;

        anofox_free_seasonality_result(&seas_result);
    }
}

// Single-argument version (convenience wrapper)
static void TsAnalyzeSeasonalityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    ListDoubleReader series_reader(values_vec, count);

    result.SetVectorType(VectorType::FLAT_VECTOR);

    vector<double> values;
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (!series_reader.ReadSingleSeries(row_idx, values, "ts_analyze_seasonality")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

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
            auto &periods_list = StructEntry(children[0]);
            auto list_data = FlatDataMut<list_entry_t>(periods_list);
            auto &list_child = ListChildMut(periods_list);
            auto current_size = ListVector::GetListSize(periods_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = seas_result.n_periods;

            ListVector::Reserve(periods_list, current_size + seas_result.n_periods);
            ListVector::SetListSize(periods_list, current_size + seas_result.n_periods);

            auto child_data = FlatDataMut<int32_t>(list_child);
            for (size_t i = 0; i < seas_result.n_periods; i++) {
                child_data[current_size + i] = seas_result.detected_periods[i];
            }
        }

        // Set scalar fields
        FlatDataMut<int32_t>(StructEntry(children[1]))[row_idx] = seas_result.primary_period;
        FlatDataMut<double>(StructEntry(children[2]))[row_idx] = seas_result.seasonal_strength;
        FlatDataMut<double>(StructEntry(children[3]))[row_idx] = seas_result.trend_strength;

        anofox_free_seasonality_result(&seas_result);
    }
}

void RegisterTsAnalyzeSeasonalityFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_analyze_set("ts_analyze_seasonality");
    // Single-argument version (convenience)
    ts_analyze_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetSeasonalityResultType(),
        TsAnalyzeSeasonalityFunction
    )));
    // Two-argument version (C++ API compatible: timestamps, values)
    ts_analyze_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::TIMESTAMP)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetSeasonalityResultType(),
        TsAnalyzeSeasonalityWithTimestampsFunction
    )));
    {
        CreateScalarFunctionInfo info(ts_analyze_set);
        FunctionDescription desc;
        desc.description = "Analyzes seasonal patterns in detail, returning amplitude, phase, strength, and period information.";
        desc.examples = {"ts_analyze_seasonality(LIST(value ORDER BY date), 7)"};
        desc.categories = {"time-series", "seasonality"};
        desc.parameter_names = {"values"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        FunctionDescription desc2;
        desc2.description = "Analyzes seasonal patterns in detail, returning amplitude, phase, strength, and period information.";
        desc2.examples = {"ts_analyze_seasonality(LIST(ts ORDER BY ts), LIST(value ORDER BY ts))"};
        desc2.categories = {"time-series", "seasonality"};
        desc2.parameter_names = {"timestamps", "values"};
        desc2.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::TIMESTAMP)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc2));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_analyze_seasonality");
    anofox_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetSeasonalityResultType(),
        TsAnalyzeSeasonalityFunction
    )));
    anofox_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::TIMESTAMP)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetSeasonalityResultType(),
        TsAnalyzeSeasonalityWithTimestampsFunction
    )));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_analyze_seasonality";
        FunctionDescription desc;
        desc.description = "Analyzes seasonal patterns in detail, returning amplitude, phase, strength, and period information.";
        desc.examples = {"ts_analyze_seasonality(LIST(value ORDER BY date), 7)"};
        desc.categories = {"time-series", "seasonality"};
        desc.parameter_names = {"values"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        FunctionDescription desc2;
        desc2.description = "Analyzes seasonal patterns in detail, returning amplitude, phase, strength, and period information.";
        desc2.examples = {"ts_analyze_seasonality(LIST(ts ORDER BY ts), LIST(value ORDER BY ts))"};
        desc2.categories = {"time-series", "seasonality"};
        desc2.parameter_names = {"timestamps", "values"};
        desc2.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::TIMESTAMP)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc2));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_classify_seasonality - Full seasonality classification with timing & modulation
// Returns: STRUCT(timing_classification, modulation_type, has_stable_timing,
//                 timing_variability, seasonal_strength, is_seasonal,
//                 cycle_strengths, weak_seasons)
// ============================================================================

static LogicalType GetSeasonalityClassificationResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("timing_classification", LogicalType(LogicalTypeId::VARCHAR)));
    children.push_back(make_pair("modulation_type", LogicalType(LogicalTypeId::VARCHAR)));
    children.push_back(make_pair("has_stable_timing", LogicalType(LogicalTypeId::BOOLEAN)));
    children.push_back(make_pair("timing_variability", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("seasonal_strength", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("is_seasonal", LogicalType(LogicalTypeId::BOOLEAN)));
    children.push_back(make_pair("cycle_strengths", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("weak_seasons", LogicalType::LIST(LogicalType(LogicalTypeId::BIGINT))));
    return LogicalType::STRUCT(std::move(children));
}

// Helper to set list of doubles in result struct
static void SetDoubleListField(Vector &list_vec, idx_t row_idx, const double *data, size_t count) {
    auto list_data = FlatDataMut<list_entry_t>(list_vec);
    auto &list_child = ListChildMut(list_vec);
    auto current_size = ListVector::GetListSize(list_vec);

    list_data[row_idx].offset = current_size;
    list_data[row_idx].length = count;

    ListVector::Reserve(list_vec, current_size + count);
    ListVector::SetListSize(list_vec, current_size + count);

    auto child_data = FlatDataMut<double>(list_child);
    for (size_t i = 0; i < count; i++) {
        child_data[current_size + i] = data[i];
    }
}

// Helper to set list of bigints in result struct
static void SetBigintListField(Vector &list_vec, idx_t row_idx, const size_t *data, size_t count) {
    auto list_data = FlatDataMut<list_entry_t>(list_vec);
    auto &list_child = ListChildMut(list_vec);
    auto current_size = ListVector::GetListSize(list_vec);

    list_data[row_idx].offset = current_size;
    list_data[row_idx].length = count;

    ListVector::Reserve(list_vec, current_size + count);
    ListVector::SetListSize(list_vec, current_size + count);

    auto child_data = FlatDataMut<int64_t>(list_child);
    for (size_t i = 0; i < count; i++) {
        child_data[current_size + i] = static_cast<int64_t>(data[i]);
    }
}

// ts_classify_seasonality(values, period, [strength_threshold], [timing_threshold])
static void TsClassifySeasonalityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    ListDoubleReader series_reader(values_vec, count);
    ScalarArgReader<double> period_reader(args.data[1], count);

    bool has_strength_threshold = (args.ColumnCount() > 2);
    unique_ptr<ScalarArgReader<double>> strength_threshold_reader;
    if (has_strength_threshold) {
        strength_threshold_reader = make_uniq<ScalarArgReader<double>>(args.data[2], count);
    }
    bool has_timing_threshold = (args.ColumnCount() > 3);
    unique_ptr<ScalarArgReader<double>> timing_threshold_reader;
    if (has_timing_threshold) {
        timing_threshold_reader = make_uniq<ScalarArgReader<double>>(args.data[3], count);
    }

    result.SetVectorType(VectorType::FLAT_VECTOR);

    vector<double> values;
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (period_reader.IsNull(row_idx) ||
            !series_reader.ReadSingleSeries(row_idx, values, "ts_classify_seasonality")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        double period = period_reader.Get(row_idx);

        double strength_threshold = 0.3;  // default
        if (has_strength_threshold && !strength_threshold_reader->IsNull(row_idx)) {
            strength_threshold = strength_threshold_reader->Get(row_idx);
        }
        double timing_threshold = 0.1;  // default
        if (has_timing_threshold && !timing_threshold_reader->IsNull(row_idx)) {
            timing_threshold = timing_threshold_reader->Get(row_idx);
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
            // CR-06/WR-04: delegate period validation to the single source
            // of truth (crates/anofox-fcst-core/src/seasonality.rs ::
            // classify_seasonality, which rejects a non-finite or
            // non-positive period as InvalidParameter before any
            // `period as usize` cast), mapped to INVALID_INPUT at the FFI
            // boundary (ffi_error_code, fixed in 86802cc for this same
            // function's FFI entry point). This removes the local
            // std::isfinite + static_cast<size_t>(2 * period) UB-prone
            // pre-check entirely and unifies this function's error wording
            // with its 8 siblings (ts_seasonal_strength,
            // ts_decompose_seasonal, etc). Insufficient-data failures
            // (error.code != INVALID_INPUT) keep returning NULL, matching
            // every other period-validating function in this file.
            if (error.code == INVALID_INPUT) {
                throw InvalidInputException("ts_classify_seasonality: %s",
                    error.message ? error.message : "invalid input");
            }
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
        FlatDataMut<string_t>(StructEntry(children[0]))[row_idx] =
            StringVector::AddString(StructEntry(children[0]), class_result.classification);

        // modulation_type (index 1)
        if (mod_success) {
            FlatDataMut<string_t>(StructEntry(children[1]))[row_idx] =
                StringVector::AddString(StructEntry(children[1]), mod_result.modulation_type);
        } else {
            FlatDataMut<string_t>(StructEntry(children[1]))[row_idx] =
                StringVector::AddString(StructEntry(children[1]), "unknown");
        }

        // has_stable_timing (index 2)
        FlatDataMut<bool>(StructEntry(children[2]))[row_idx] = class_result.has_stable_timing;

        // timing_variability (index 3)
        FlatDataMut<double>(StructEntry(children[3]))[row_idx] = class_result.timing_variability;

        // seasonal_strength (index 4)
        FlatDataMut<double>(StructEntry(children[4]))[row_idx] = class_result.seasonal_strength;

        // is_seasonal (index 5)
        FlatDataMut<bool>(StructEntry(children[5]))[row_idx] = class_result.is_seasonal;

        // cycle_strengths (index 6)
        SetDoubleListField(StructEntry(children[6]), row_idx, class_result.cycle_strengths, class_result.n_cycle_strengths);

        // weak_seasons (index 7)
        SetBigintListField(StructEntry(children[7]), row_idx, class_result.weak_seasons, class_result.n_weak_seasons);

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
    ts_classify_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    )));

    // ts_classify_seasonality(values, period, strength_threshold) -> STRUCT
    ts_classify_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE)},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    )));

    // ts_classify_seasonality(values, period, strength_threshold, timing_threshold) -> STRUCT
    ts_classify_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE)},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    )));

    {
        CreateScalarFunctionInfo info(ts_classify_set);
        FunctionDescription desc;
        desc.description = "Classifies the seasonality type (timing and modulation) of a time series, returning timing_classification, modulation_type, and seasonal strength.";
        desc.examples = {"ts_classify_seasonality(LIST(value ORDER BY date), 7)"};
        desc.categories = {"time-series", "seasonality"};
        desc.parameter_names = {"values"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    // Also register with anofox_ prefix
    ScalarFunctionSet anofox_classify_set("anofox_fcst_ts_classify_seasonality");
    anofox_classify_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    )));
    anofox_classify_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE)},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    )));
    anofox_classify_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE)},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    )));
    {
        CreateScalarFunctionInfo info(anofox_classify_set);
        info.alias_of = "ts_classify_seasonality";
        FunctionDescription desc;
        desc.description = "Classifies the seasonality type (timing and modulation) of a time series, returning timing_classification, modulation_type, and seasonal strength.";
        desc.examples = {"ts_classify_seasonality(LIST(value ORDER BY date), 7)"};
        desc.categories = {"time-series", "seasonality"};
        desc.parameter_names = {"values"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

} // namespace duckdb
