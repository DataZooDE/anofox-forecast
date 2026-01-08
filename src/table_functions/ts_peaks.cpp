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
// ts_detect_peaks - Detect peaks in time series
// Returns: STRUCT(peaks STRUCT[], n_peaks, inter_peak_distances, mean_period)
// ============================================================================

static LogicalType GetPeakDetectionResultType() {
    // Inner struct for each peak
    child_list_t<LogicalType> peak_children;
    peak_children.push_back(make_pair("index", LogicalType::BIGINT));
    peak_children.push_back(make_pair("time", LogicalType::DOUBLE));
    peak_children.push_back(make_pair("value", LogicalType::DOUBLE));
    peak_children.push_back(make_pair("prominence", LogicalType::DOUBLE));
    auto peak_type = LogicalType::STRUCT(std::move(peak_children));

    // Outer result struct
    child_list_t<LogicalType> children;
    children.push_back(make_pair("peaks", LogicalType::LIST(peak_type)));
    children.push_back(make_pair("n_peaks", LogicalType::BIGINT));
    children.push_back(make_pair("inter_peak_distances", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("mean_period", LogicalType::DOUBLE));
    return LogicalType::STRUCT(std::move(children));
}

static void TsDetectPeaksFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    // Optional parameters
    double min_distance = 0.0;
    double min_prominence = 0.0;
    bool smooth_first = false;

    if (args.ColumnCount() > 1 && !FlatVector::IsNull(args.data[1], 0)) {
        min_distance = FlatVector::GetData<double>(args.data[1])[0];
    }
    if (args.ColumnCount() > 2 && !FlatVector::IsNull(args.data[2], 0)) {
        min_prominence = FlatVector::GetData<double>(args.data[2])[0];
    }
    if (args.ColumnCount() > 3 && !FlatVector::IsNull(args.data[3], 0)) {
        smooth_first = FlatVector::GetData<bool>(args.data[3])[0];
    }

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, row_idx, values);

        PeakDetectionResultFFI peak_result;
        memset(&peak_result, 0, sizeof(peak_result));
        AnofoxError error;

        bool success = anofox_ts_detect_peaks(
            values.data(),
            values.size(),
            min_distance,
            min_prominence,
            smooth_first,
            &peak_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);

        // Set peaks list
        {
            auto &peaks_list = *children[0];
            auto list_data = FlatVector::GetData<list_entry_t>(peaks_list);
            auto &list_child = ListVector::GetEntry(peaks_list);
            auto current_size = ListVector::GetListSize(peaks_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = peak_result.n_peaks;

            ListVector::Reserve(peaks_list, current_size + peak_result.n_peaks);
            ListVector::SetListSize(peaks_list, current_size + peak_result.n_peaks);

            auto &struct_entries = StructVector::GetEntries(list_child);
            for (size_t i = 0; i < peak_result.n_peaks; i++) {
                FlatVector::GetData<int64_t>(*struct_entries[0])[current_size + i] = peak_result.peaks[i].index;
                FlatVector::GetData<double>(*struct_entries[1])[current_size + i] = peak_result.peaks[i].time;
                FlatVector::GetData<double>(*struct_entries[2])[current_size + i] = peak_result.peaks[i].value;
                FlatVector::GetData<double>(*struct_entries[3])[current_size + i] = peak_result.peaks[i].prominence;
            }
        }

        // Set n_peaks
        FlatVector::GetData<int64_t>(*children[1])[row_idx] = peak_result.n_peaks;

        // Set inter_peak_distances list
        {
            auto &distances_list = *children[2];
            auto list_data = FlatVector::GetData<list_entry_t>(distances_list);
            auto &list_child = ListVector::GetEntry(distances_list);
            auto current_size = ListVector::GetListSize(distances_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = peak_result.n_distances;

            ListVector::Reserve(distances_list, current_size + peak_result.n_distances);
            ListVector::SetListSize(distances_list, current_size + peak_result.n_distances);

            auto child_data = FlatVector::GetData<double>(list_child);
            for (size_t i = 0; i < peak_result.n_distances; i++) {
                child_data[current_size + i] = peak_result.inter_peak_distances[i];
            }
        }

        // Set mean_period
        FlatVector::GetData<double>(*children[3])[row_idx] = peak_result.mean_period;

        anofox_free_peak_detection_result(&peak_result);
    }
}

void RegisterTsDetectPeaksFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_peaks_set("ts_detect_peaks");
    // Single-argument version
    ts_peaks_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        GetPeakDetectionResultType(),
        TsDetectPeaksFunction
    ));
    // With min_distance
    ts_peaks_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        GetPeakDetectionResultType(),
        TsDetectPeaksFunction
    ));
    // With min_distance, min_prominence
    ts_peaks_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE},
        GetPeakDetectionResultType(),
        TsDetectPeaksFunction
    ));
    // With min_distance, min_prominence, smooth_first
    ts_peaks_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::BOOLEAN},
        GetPeakDetectionResultType(),
        TsDetectPeaksFunction
    ));
    loader.RegisterFunction(ts_peaks_set);
}

// ============================================================================
// ts_analyze_peak_timing - Analyze peak timing variability
// Returns: STRUCT(peak_times[], peak_values[], normalized_timing[], n_peaks,
//                 mean_timing, std_timing, range_timing, variability_score,
//                 timing_trend, is_stable)
// ============================================================================

static LogicalType GetPeakTimingResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("peak_times", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("peak_values", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("normalized_timing", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("n_peaks", LogicalType::BIGINT));
    children.push_back(make_pair("mean_timing", LogicalType::DOUBLE));
    children.push_back(make_pair("std_timing", LogicalType::DOUBLE));
    children.push_back(make_pair("range_timing", LogicalType::DOUBLE));
    children.push_back(make_pair("variability_score", LogicalType::DOUBLE));
    children.push_back(make_pair("timing_trend", LogicalType::DOUBLE));
    children.push_back(make_pair("is_stable", LogicalType::BOOLEAN));
    return LogicalType::STRUCT(std::move(children));
}

static void TsAnalyzePeakTimingFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto &period_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx) || FlatVector::IsNull(period_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(values_vec, row_idx, values);
        double period = FlatVector::GetData<double>(period_vec)[row_idx];

        PeakTimingResultFFI timing_result;
        memset(&timing_result, 0, sizeof(timing_result));
        AnofoxError error;

        bool success = anofox_ts_analyze_peak_timing(
            values.data(),
            values.size(),
            period,
            &timing_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
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

        set_double_list(0, timing_result.peak_times, timing_result.n_peaks);
        set_double_list(1, timing_result.peak_values, timing_result.n_peaks);
        set_double_list(2, timing_result.normalized_timing, timing_result.n_peaks);

        FlatVector::GetData<int64_t>(*children[3])[row_idx] = timing_result.n_peaks;
        FlatVector::GetData<double>(*children[4])[row_idx] = timing_result.mean_timing;
        FlatVector::GetData<double>(*children[5])[row_idx] = timing_result.std_timing;
        FlatVector::GetData<double>(*children[6])[row_idx] = timing_result.range_timing;
        FlatVector::GetData<double>(*children[7])[row_idx] = timing_result.variability_score;
        FlatVector::GetData<double>(*children[8])[row_idx] = timing_result.timing_trend;
        FlatVector::GetData<bool>(*children[9])[row_idx] = timing_result.is_stable;

        anofox_free_peak_timing_result(&timing_result);
    }
}

void RegisterTsAnalyzePeakTimingFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_peak_timing_set("ts_analyze_peak_timing");
    ts_peak_timing_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        GetPeakTimingResultType(),
        TsAnalyzePeakTimingFunction
    ));
    loader.RegisterFunction(ts_peak_timing_set);
}

} // namespace duckdb
