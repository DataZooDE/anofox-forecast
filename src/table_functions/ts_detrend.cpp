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
// ts_detrend - Remove trend from time series
// Returns: STRUCT(trend[], detrended[], method, coefficients[], rss, n_params)
// ============================================================================

static LogicalType GetDetrendResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("trend", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("detrended", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("method", LogicalType::VARCHAR));
    children.push_back(make_pair("coefficients", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("rss", LogicalType::DOUBLE));
    children.push_back(make_pair("n_params", LogicalType::BIGINT));
    return LogicalType::STRUCT(std::move(children));
}

static void TsDetrendFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    const char *method_str = nullptr;
    if (args.ColumnCount() > 1 && !FlatVector::IsNull(args.data[1], 0)) {
        auto method_data = FlatVector::GetData<string_t>(args.data[1]);
        method_str = method_data[0].GetData();
    }

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, row_idx, values);

        DetrendResultFFI detrend_result;
        memset(&detrend_result, 0, sizeof(detrend_result));
        AnofoxError error;

        bool success = anofox_ts_detrend(
            values.data(),
            values.size(),
            method_str,
            &detrend_result,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_detrend failed: %s", error.message);
        }

        auto &children = StructVector::GetEntries(result);

        // Helper to set a list of doubles
        auto set_double_list = [&](int child_idx, double *data, size_t n) {
            auto &list_vec = *children[child_idx];
            auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
            auto &list_child = ListVector::GetEntry(list_vec);
            auto current_size = ListVector::GetListSize(list_vec);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = n;

            ListVector::Reserve(list_vec, current_size + n);
            ListVector::SetListSize(list_vec, current_size + n);

            auto child_data = FlatVector::GetData<double>(list_child);
            for (size_t i = 0; i < n; i++) {
                child_data[current_size + i] = data[i];
            }
        };

        set_double_list(0, detrend_result.trend, detrend_result.length);
        set_double_list(1, detrend_result.detrended, detrend_result.length);
        FlatVector::GetData<string_t>(*children[2])[row_idx] = StringVector::AddString(*children[2], detrend_result.method);
        set_double_list(3, detrend_result.coefficients, detrend_result.n_coefficients);
        FlatVector::GetData<double>(*children[4])[row_idx] = detrend_result.rss;
        FlatVector::GetData<int64_t>(*children[5])[row_idx] = detrend_result.n_params;

        anofox_free_detrend_result(&detrend_result);
    }
}

void RegisterTsDetrendFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_detrend_set("ts_detrend");
    // Single-argument version (auto method)
    ts_detrend_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        GetDetrendResultType(),
        TsDetrendFunction
    ));
    // With method
    ts_detrend_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        GetDetrendResultType(),
        TsDetrendFunction
    ));
    loader.RegisterFunction(ts_detrend_set);
}

// ============================================================================
// ts_decompose_seasonal - Seasonal decomposition (additive/multiplicative)
// Returns: STRUCT(trend[], seasonal[], remainder[], period, method)
// ============================================================================

static LogicalType GetDecomposeResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("trend", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("seasonal", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("remainder", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("period", LogicalType::DOUBLE));
    children.push_back(make_pair("method", LogicalType::VARCHAR));
    return LogicalType::STRUCT(std::move(children));
}

static void TsDecomposeSeasonalFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto &period_vec = args.data[1];
    idx_t count = args.size();

    const char *method_str = nullptr;
    if (args.ColumnCount() > 2 && !FlatVector::IsNull(args.data[2], 0)) {
        auto method_data = FlatVector::GetData<string_t>(args.data[2]);
        method_str = method_data[0].GetData();
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

        DecomposeResultFFI decompose_result;
        memset(&decompose_result, 0, sizeof(decompose_result));
        AnofoxError error;

        bool success = anofox_ts_decompose(
            values.data(),
            values.size(),
            period,
            method_str,
            &decompose_result,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_decompose_seasonal failed: %s", error.message);
        }

        auto &children = StructVector::GetEntries(result);

        // Helper to set a list of doubles
        auto set_double_list = [&](int child_idx, double *data, size_t n) {
            auto &list_vec = *children[child_idx];
            auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
            auto &list_child = ListVector::GetEntry(list_vec);
            auto current_size = ListVector::GetListSize(list_vec);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = n;

            ListVector::Reserve(list_vec, current_size + n);
            ListVector::SetListSize(list_vec, current_size + n);

            auto child_data = FlatVector::GetData<double>(list_child);
            for (size_t i = 0; i < n; i++) {
                child_data[current_size + i] = data[i];
            }
        };

        set_double_list(0, decompose_result.trend, decompose_result.length);
        set_double_list(1, decompose_result.seasonal, decompose_result.length);
        set_double_list(2, decompose_result.remainder, decompose_result.length);
        FlatVector::GetData<double>(*children[3])[row_idx] = decompose_result.period;
        FlatVector::GetData<string_t>(*children[4])[row_idx] = StringVector::AddString(*children[4], decompose_result.method);

        anofox_free_decompose_result(&decompose_result);
    }
}

void RegisterTsDecomposeSeasonalFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_decompose_set("ts_decompose_seasonal");
    // With period (additive by default)
    ts_decompose_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        GetDecomposeResultType(),
        TsDecomposeSeasonalFunction
    ));
    // With period and method
    ts_decompose_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::VARCHAR},
        GetDecomposeResultType(),
        TsDecomposeSeasonalFunction
    ));
    loader.RegisterFunction(ts_decompose_set);
}

// ============================================================================
// ts_seasonal_strength - Compute seasonal strength
// Returns: DOUBLE (seasonal strength 0-1)
// ============================================================================

static void TsSeasonalStrengthFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto &period_vec = args.data[1];
    idx_t count = args.size();

    const char *method_str = nullptr;
    if (args.ColumnCount() > 2 && !FlatVector::IsNull(args.data[2], 0)) {
        auto method_data = FlatVector::GetData<string_t>(args.data[2]);
        method_str = method_data[0].GetData();
    }

    // Handle constant vectors properly
    UnifiedVectorFormat period_format;
    period_vec.ToUnifiedFormat(count, period_format);
    auto period_data = UnifiedVectorFormat::GetData<double>(period_format);

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto period_idx = period_format.sel->get_index(row_idx);
        if (FlatVector::IsNull(values_vec, row_idx) || !period_format.validity.RowIsValid(period_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, row_idx, values);
        double period = period_data[period_idx];

        double strength = 0.0;
        AnofoxError error;

        bool success = anofox_ts_seasonal_strength(
            values.data(),
            values.size(),
            period,
            method_str,
            &strength,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_seasonal_strength failed: %s", error.message);
        }

        result_data[row_idx] = strength;
    }
}

void RegisterTsSeasonalStrengthFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_strength_set("ts_seasonal_strength");
    // With period (variance method by default)
    ts_strength_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        LogicalType::DOUBLE,
        TsSeasonalStrengthFunction
    ));
    // With period and method
    ts_strength_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::VARCHAR},
        LogicalType::DOUBLE,
        TsSeasonalStrengthFunction
    ));
    loader.RegisterFunction(ts_strength_set);
}

// ============================================================================
// ts_seasonal_strength_windowed - Compute windowed seasonal strength
// Returns: LIST(DOUBLE) (strength at each window)
// ============================================================================

static void TsSeasonalStrengthWindowedFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto &period_vec = args.data[1];
    idx_t count = args.size();

    double window_size = 0.0;
    const char *method_str = nullptr;

    if (args.ColumnCount() > 2 && !FlatVector::IsNull(args.data[2], 0)) {
        window_size = FlatVector::GetData<double>(args.data[2])[0];
    }
    if (args.ColumnCount() > 3 && !FlatVector::IsNull(args.data[3], 0)) {
        auto method_data = FlatVector::GetData<string_t>(args.data[3]);
        method_str = method_data[0].GetData();
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

        double *strengths = nullptr;
        size_t n_windows = 0;
        AnofoxError error;

        bool success = anofox_ts_seasonal_strength_windowed(
            values.data(),
            values.size(),
            period,
            window_size,
            method_str,
            &strengths,
            &n_windows,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_seasonal_strength_windowed failed: %s", error.message);
        }

        auto list_data = FlatVector::GetData<list_entry_t>(result);
        auto &list_child = ListVector::GetEntry(result);
        auto current_size = ListVector::GetListSize(result);

        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = n_windows;

        ListVector::Reserve(result, current_size + n_windows);
        ListVector::SetListSize(result, current_size + n_windows);

        auto child_data = FlatVector::GetData<double>(list_child);
        for (size_t i = 0; i < n_windows; i++) {
            child_data[current_size + i] = strengths[i];
        }

        if (strengths) {
            anofox_free_double_array(strengths);
        }
    }
}

void RegisterTsSeasonalStrengthWindowedFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_strength_win_set("ts_seasonal_strength_windowed");
    // With period
    ts_strength_win_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsSeasonalStrengthWindowedFunction
    ));
    // With period and window_size
    ts_strength_win_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsSeasonalStrengthWindowedFunction
    ));
    // With period, window_size, and method
    ts_strength_win_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::VARCHAR},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsSeasonalStrengthWindowedFunction
    ));
    loader.RegisterFunction(ts_strength_win_set);
}

// ============================================================================
// ts_classify_seasonality - Classify seasonality type
// Returns: STRUCT(is_seasonal, has_stable_timing, timing_variability,
//                 seasonal_strength, cycle_strengths[], weak_seasons[], classification)
// ============================================================================

static LogicalType GetSeasonalityClassificationResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("is_seasonal", LogicalType::BOOLEAN));
    children.push_back(make_pair("has_stable_timing", LogicalType::BOOLEAN));
    children.push_back(make_pair("timing_variability", LogicalType::DOUBLE));
    children.push_back(make_pair("seasonal_strength", LogicalType::DOUBLE));
    children.push_back(make_pair("cycle_strengths", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("weak_seasons", LogicalType::LIST(LogicalType::BIGINT)));
    children.push_back(make_pair("classification", LogicalType::VARCHAR));
    return LogicalType::STRUCT(std::move(children));
}

static void TsClassifySeasonalityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto &period_vec = args.data[1];
    idx_t count = args.size();

    double strength_threshold = 0.0;
    double timing_threshold = 0.0;

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
            throw InvalidInputException("ts_classify_seasonality failed: %s", error.message);
        }

        auto &children = StructVector::GetEntries(result);

        FlatVector::GetData<bool>(*children[0])[row_idx] = class_result.is_seasonal;
        FlatVector::GetData<bool>(*children[1])[row_idx] = class_result.has_stable_timing;
        FlatVector::GetData<double>(*children[2])[row_idx] = class_result.timing_variability;
        FlatVector::GetData<double>(*children[3])[row_idx] = class_result.seasonal_strength;

        // Set cycle_strengths list
        {
            auto &list_vec = *children[4];
            auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
            auto &list_child = ListVector::GetEntry(list_vec);
            auto current_size = ListVector::GetListSize(list_vec);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = class_result.n_cycle_strengths;

            ListVector::Reserve(list_vec, current_size + class_result.n_cycle_strengths);
            ListVector::SetListSize(list_vec, current_size + class_result.n_cycle_strengths);

            auto child_data = FlatVector::GetData<double>(list_child);
            for (size_t i = 0; i < class_result.n_cycle_strengths; i++) {
                child_data[current_size + i] = class_result.cycle_strengths[i];
            }
        }

        // Set weak_seasons list
        {
            auto &list_vec = *children[5];
            auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
            auto &list_child = ListVector::GetEntry(list_vec);
            auto current_size = ListVector::GetListSize(list_vec);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = class_result.n_weak_seasons;

            ListVector::Reserve(list_vec, current_size + class_result.n_weak_seasons);
            ListVector::SetListSize(list_vec, current_size + class_result.n_weak_seasons);

            auto child_data = FlatVector::GetData<int64_t>(list_child);
            for (size_t i = 0; i < class_result.n_weak_seasons; i++) {
                child_data[current_size + i] = class_result.weak_seasons[i];
            }
        }

        FlatVector::GetData<string_t>(*children[6])[row_idx] = StringVector::AddString(*children[6], class_result.classification);

        anofox_free_seasonality_classification_result(&class_result);
    }
}

void RegisterTsClassifySeasonalityFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_classify_set("ts_classify_seasonality");
    // With period
    ts_classify_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    ));
    // With period and strength_threshold
    ts_classify_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    ));
    // With period, strength_threshold, and timing_threshold
    ts_classify_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetSeasonalityClassificationResultType(),
        TsClassifySeasonalityFunction
    ));
    loader.RegisterFunction(ts_classify_set);
}

// ============================================================================
// ts_detect_seasonality_changes - Detect changes in seasonality
// Returns: STRUCT(change_points[], n_changes, strength_curve[])
// ============================================================================

static LogicalType GetChangeDetectionResultType() {
    // Inner struct for change points
    child_list_t<LogicalType> cp_children;
    cp_children.push_back(make_pair("index", LogicalType::BIGINT));
    cp_children.push_back(make_pair("time", LogicalType::DOUBLE));
    cp_children.push_back(make_pair("change_type", LogicalType::VARCHAR));
    cp_children.push_back(make_pair("strength_before", LogicalType::DOUBLE));
    cp_children.push_back(make_pair("strength_after", LogicalType::DOUBLE));
    auto cp_type = LogicalType::STRUCT(std::move(cp_children));

    child_list_t<LogicalType> children;
    children.push_back(make_pair("change_points", LogicalType::LIST(cp_type)));
    children.push_back(make_pair("n_changes", LogicalType::BIGINT));
    children.push_back(make_pair("strength_curve", LogicalType::LIST(LogicalType::DOUBLE)));
    return LogicalType::STRUCT(std::move(children));
}

static void TsDetectSeasonalityChangesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto &period_vec = args.data[1];
    idx_t count = args.size();

    double threshold = 0.0;
    double window_size = 0.0;
    double min_duration = 0.0;

    if (args.ColumnCount() > 2 && !FlatVector::IsNull(args.data[2], 0)) {
        threshold = FlatVector::GetData<double>(args.data[2])[0];
    }
    if (args.ColumnCount() > 3 && !FlatVector::IsNull(args.data[3], 0)) {
        window_size = FlatVector::GetData<double>(args.data[3])[0];
    }
    if (args.ColumnCount() > 4 && !FlatVector::IsNull(args.data[4], 0)) {
        min_duration = FlatVector::GetData<double>(args.data[4])[0];
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

        ChangeDetectionResultFFI change_result;
        memset(&change_result, 0, sizeof(change_result));
        AnofoxError error;

        bool success = anofox_ts_detect_seasonality_changes(
            values.data(),
            values.size(),
            period,
            threshold,
            window_size,
            min_duration,
            &change_result,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_detect_seasonality_changes failed: %s", error.message);
        }

        auto &children = StructVector::GetEntries(result);

        // Set change_points list
        {
            auto &cp_list = *children[0];
            auto list_data = FlatVector::GetData<list_entry_t>(cp_list);
            auto &list_child = ListVector::GetEntry(cp_list);
            auto current_size = ListVector::GetListSize(cp_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = change_result.n_changes;

            ListVector::Reserve(cp_list, current_size + change_result.n_changes);
            ListVector::SetListSize(cp_list, current_size + change_result.n_changes);

            auto &struct_entries = StructVector::GetEntries(list_child);
            for (size_t i = 0; i < change_result.n_changes; i++) {
                FlatVector::GetData<int64_t>(*struct_entries[0])[current_size + i] = change_result.change_points[i].index;
                FlatVector::GetData<double>(*struct_entries[1])[current_size + i] = change_result.change_points[i].time;
                FlatVector::GetData<string_t>(*struct_entries[2])[current_size + i] =
                    StringVector::AddString(*struct_entries[2], change_result.change_points[i].change_type);
                FlatVector::GetData<double>(*struct_entries[3])[current_size + i] = change_result.change_points[i].strength_before;
                FlatVector::GetData<double>(*struct_entries[4])[current_size + i] = change_result.change_points[i].strength_after;
            }
        }

        FlatVector::GetData<int64_t>(*children[1])[row_idx] = change_result.n_changes;

        // Set strength_curve list
        {
            auto &curve_list = *children[2];
            auto list_data = FlatVector::GetData<list_entry_t>(curve_list);
            auto &list_child = ListVector::GetEntry(curve_list);
            auto current_size = ListVector::GetListSize(curve_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = change_result.n_strength_curve;

            ListVector::Reserve(curve_list, current_size + change_result.n_strength_curve);
            ListVector::SetListSize(curve_list, current_size + change_result.n_strength_curve);

            auto child_data = FlatVector::GetData<double>(list_child);
            for (size_t i = 0; i < change_result.n_strength_curve; i++) {
                child_data[current_size + i] = change_result.strength_curve[i];
            }
        }

        anofox_free_change_detection_result(&change_result);
    }
}

void RegisterTsDetectSeasonalityChangesFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_changes_set("ts_detect_seasonality_changes");
    // With period
    ts_changes_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        GetChangeDetectionResultType(),
        TsDetectSeasonalityChangesFunction
    ));
    // With period and threshold
    ts_changes_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetChangeDetectionResultType(),
        TsDetectSeasonalityChangesFunction
    ));
    // With period, threshold, and window_size
    ts_changes_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetChangeDetectionResultType(),
        TsDetectSeasonalityChangesFunction
    ));
    // With period, threshold, window_size, and min_duration
    ts_changes_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetChangeDetectionResultType(),
        TsDetectSeasonalityChangesFunction
    ));
    loader.RegisterFunction(ts_changes_set);
}

// ============================================================================
// ts_instantaneous_period - Compute instantaneous period using Hilbert transform
// Returns: STRUCT(periods[], frequencies[], amplitudes[])
// ============================================================================

static LogicalType GetInstantaneousPeriodResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("periods", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("frequencies", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("amplitudes", LogicalType::LIST(LogicalType::DOUBLE)));
    return LogicalType::STRUCT(std::move(children));
}

static void TsInstantaneousPeriodFunction(DataChunk &args, ExpressionState &state, Vector &result) {
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

        InstantaneousPeriodResultFFI inst_result;
        memset(&inst_result, 0, sizeof(inst_result));
        AnofoxError error;

        bool success = anofox_ts_instantaneous_period(
            values.data(),
            values.size(),
            &inst_result,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_instantaneous_period failed: %s", error.message);
        }

        auto &children = StructVector::GetEntries(result);

        // Helper to set a list of doubles
        auto set_double_list = [&](int child_idx, double *data, size_t n) {
            auto &list_vec = *children[child_idx];
            auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
            auto &list_child = ListVector::GetEntry(list_vec);
            auto current_size = ListVector::GetListSize(list_vec);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = n;

            ListVector::Reserve(list_vec, current_size + n);
            ListVector::SetListSize(list_vec, current_size + n);

            auto child_data = FlatVector::GetData<double>(list_child);
            for (size_t i = 0; i < n; i++) {
                child_data[current_size + i] = data[i];
            }
        };

        set_double_list(0, inst_result.periods, inst_result.length);
        set_double_list(1, inst_result.frequencies, inst_result.length);
        set_double_list(2, inst_result.amplitudes, inst_result.length);

        anofox_free_instantaneous_period_result(&inst_result);
    }
}

void RegisterTsInstantaneousPeriodFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_inst_period_set("ts_instantaneous_period");
    ts_inst_period_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        GetInstantaneousPeriodResultType(),
        TsInstantaneousPeriodFunction
    ));
    loader.RegisterFunction(ts_inst_period_set);
}

// ============================================================================
// ts_detect_amplitude_modulation - Detect amplitude modulation
// Returns: STRUCT(is_seasonal, seasonal_strength, has_modulation, modulation_type,
//                 modulation_score, amplitude_trend, wavelet_amplitude[], time_points[], scale)
// ============================================================================

static LogicalType GetAmplitudeModulationResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("is_seasonal", LogicalType::BOOLEAN));
    children.push_back(make_pair("seasonal_strength", LogicalType::DOUBLE));
    children.push_back(make_pair("has_modulation", LogicalType::BOOLEAN));
    children.push_back(make_pair("modulation_type", LogicalType::VARCHAR));
    children.push_back(make_pair("modulation_score", LogicalType::DOUBLE));
    children.push_back(make_pair("amplitude_trend", LogicalType::DOUBLE));
    children.push_back(make_pair("wavelet_amplitude", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("time_points", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("scale", LogicalType::DOUBLE));
    return LogicalType::STRUCT(std::move(children));
}

static void TsDetectAmplitudeModulationFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto &period_vec = args.data[1];
    idx_t count = args.size();

    double modulation_threshold = 0.0;
    double seasonality_threshold = 0.0;

    if (args.ColumnCount() > 2 && !FlatVector::IsNull(args.data[2], 0)) {
        modulation_threshold = FlatVector::GetData<double>(args.data[2])[0];
    }
    if (args.ColumnCount() > 3 && !FlatVector::IsNull(args.data[3], 0)) {
        seasonality_threshold = FlatVector::GetData<double>(args.data[3])[0];
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

        AmplitudeModulationResultFFI am_result;
        memset(&am_result, 0, sizeof(am_result));
        AnofoxError error;

        bool success = anofox_ts_detect_amplitude_modulation(
            values.data(),
            values.size(),
            period,
            modulation_threshold,
            seasonality_threshold,
            &am_result,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_detect_amplitude_modulation failed: %s", error.message);
        }

        auto &children = StructVector::GetEntries(result);

        FlatVector::GetData<bool>(*children[0])[row_idx] = am_result.is_seasonal;
        FlatVector::GetData<double>(*children[1])[row_idx] = am_result.seasonal_strength;
        FlatVector::GetData<bool>(*children[2])[row_idx] = am_result.has_modulation;
        FlatVector::GetData<string_t>(*children[3])[row_idx] = StringVector::AddString(*children[3], am_result.modulation_type);
        FlatVector::GetData<double>(*children[4])[row_idx] = am_result.modulation_score;
        FlatVector::GetData<double>(*children[5])[row_idx] = am_result.amplitude_trend;

        // Helper to set a list of doubles
        auto set_double_list = [&](int child_idx, double *data, size_t n) {
            auto &list_vec = *children[child_idx];
            auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
            auto &list_child = ListVector::GetEntry(list_vec);
            auto current_size = ListVector::GetListSize(list_vec);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = n;

            ListVector::Reserve(list_vec, current_size + n);
            ListVector::SetListSize(list_vec, current_size + n);

            auto child_data = FlatVector::GetData<double>(list_child);
            for (size_t i = 0; i < n; i++) {
                child_data[current_size + i] = data[i];
            }
        };

        set_double_list(6, am_result.wavelet_amplitude, am_result.n_points);
        set_double_list(7, am_result.time_points, am_result.n_points);
        FlatVector::GetData<double>(*children[8])[row_idx] = am_result.scale;

        anofox_free_amplitude_modulation_result(&am_result);
    }
}

void RegisterTsDetectAmplitudeModulationFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_am_set("ts_detect_amplitude_modulation");
    // With period
    ts_am_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        GetAmplitudeModulationResultType(),
        TsDetectAmplitudeModulationFunction
    ));
    // With period and modulation_threshold
    ts_am_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetAmplitudeModulationResultType(),
        TsDetectAmplitudeModulationFunction
    ));
    // With period, modulation_threshold, and seasonality_threshold
    ts_am_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetAmplitudeModulationResultType(),
        TsDetectAmplitudeModulationFunction
    ));
    loader.RegisterFunction(ts_am_set);
}

} // namespace duckdb
