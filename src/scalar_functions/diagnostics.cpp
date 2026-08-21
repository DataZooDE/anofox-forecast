/// Statistical diagnostic scalar functions for the anofox-forecast DuckDB extension.
///
/// This file implements:
///   - ts_adf(series LIST(DOUBLE) [, max_lags INTEGER]) → STRUCT(...)
///   - RegisterTsAdfFunction(ExtensionLoader&)
///
/// It follows the STRUCT-return pattern established in bootstrap.cpp and
/// the ExtractListAsDouble helper pattern from the same file.
///
/// STRUCT field order for ts_adf (STAT-01) — fixed; plans 01-2/01-3 depend on it:
///   statistic DOUBLE, p_value DOUBLE, lags BIGINT, is_stationary BOOLEAN,
///   cv_1pct DOUBLE, cv_5pct DOUBLE, cv_10pct DOUBLE

#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/types/vector.hpp"

namespace duckdb {

// ============================================================================
// Helper: extract LIST(DOUBLE) entries into a std::vector<double>.
// NULL entries in the child vector are silently skipped (consistent with the
// NaN-for-NULL handling in the FFI build_values helper).
// ============================================================================

static void ExtractListAsDoubleLocal(Vector &list_vec, idx_t row_idx,
                                     vector<double> &out_values) {
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
// ts_adf(series LIST(DOUBLE)) → STRUCT(statistic, p_value, lags, is_stationary,
//                                      cv_1pct, cv_5pct, cv_10pct)
// ts_adf(series LIST(DOUBLE), max_lags INTEGER) → same STRUCT
//
// The STRUCT field order must match RegisterTsAdfFunction's child_list_t order
// and the AnofoxStationarityResult field layout in anofox_fcst_ffi.h.
// ============================================================================

static void TsAdfFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    // Optional second argument: max_lags INTEGER (default -1 → auto)
    UnifiedVectorFormat max_lags_data;
    bool has_max_lags = (args.ColumnCount() >= 2);
    if (has_max_lags) {
        args.data[1].ToUnifiedFormat(count, max_lags_data);
    }

    // Get STRUCT output entry vectors (order matches child_list_t in RegisterTsAdfFunction)
    auto &struct_entries = StructVector::GetEntries(result);
    auto &stat_vec       = *struct_entries[0]; // statistic DOUBLE
    auto &pval_vec       = *struct_entries[1]; // p_value DOUBLE
    auto &lags_vec       = *struct_entries[2]; // lags BIGINT
    auto &istat_vec      = *struct_entries[3]; // is_stationary BOOLEAN
    auto &cv1_vec        = *struct_entries[4]; // cv_1pct DOUBLE
    auto &cv5_vec        = *struct_entries[5]; // cv_5pct DOUBLE
    auto &cv10_vec       = *struct_entries[6]; // cv_10pct DOUBLE

    auto stat_data  = FlatVector::GetData<double>(stat_vec);
    auto pval_data  = FlatVector::GetData<double>(pval_vec);
    auto lags_data  = FlatVector::GetData<int64_t>(lags_vec);
    auto istat_data = FlatVector::GetData<bool>(istat_vec);
    auto cv1_data   = FlatVector::GetData<double>(cv1_vec);
    auto cv5_data   = FlatVector::GetData<double>(cv5_vec);
    auto cv10_data  = FlatVector::GetData<double>(cv10_vec);

    vector<double> series;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        // NULL list → NULL STRUCT (Threat T-01-03 partial: zero-length handled in FFI)
        if (FlatVector::IsNull(values_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Resolve max_lags for this row (-1 = auto)
        int32_t max_lags_val = -1;
        if (has_max_lags) {
            auto ml_idx = max_lags_data.sel->get_index(row_idx);
            if (max_lags_data.validity.RowIsValid(ml_idx)) {
                max_lags_val = UnifiedVectorFormat::GetData<int32_t>(max_lags_data)[ml_idx];
            }
        }

        ExtractListAsDoubleLocal(values_vec, row_idx, series);

        AnofoxStationarityResult r = {};
        AnofoxError err = {};
        bool ok = anofox_ts_adf(
            series.data(),
            /* validity= */ nullptr,   // already filtered by ExtractListAsDoubleLocal
            series.size(),
            static_cast<int>(max_lags_val),
            &r, &err
        );

        if (!ok) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        stat_data[row_idx]  = r.statistic;
        pval_data[row_idx]  = r.p_value;
        lags_data[row_idx]  = static_cast<int64_t>(r.lags);
        istat_data[row_idx] = r.is_stationary;
        cv1_data[row_idx]   = r.cv_1pct;
        cv5_data[row_idx]   = r.cv_5pct;
        cv10_data[row_idx]  = r.cv_10pct;
    }
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsAdfFunction(ExtensionLoader &loader) {
    // STRUCT return type — field order must stay fixed (plans 01-2/01-3 extend this file)
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("statistic",     LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("p_value",       LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("lags",          LogicalType(LogicalTypeId::BIGINT)));
    struct_children.push_back(make_pair("is_stationary", LogicalType(LogicalTypeId::BOOLEAN)));
    struct_children.push_back(make_pair("cv_1pct",       LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("cv_5pct",       LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("cv_10pct",      LogicalType(LogicalTypeId::DOUBLE)));
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    // 1-arg overload: ts_adf(series)
    ScalarFunctionSet adf_set("ts_adf");
    adf_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        result_type,
        TsAdfFunction
    ));
    // 2-arg overload: ts_adf(series, max_lags)
    adf_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER)},
        result_type,
        TsAdfFunction
    ));
    {
        CreateScalarFunctionInfo info(adf_set);
        FunctionDescription desc;
        desc.description =
            "Augmented Dickey-Fuller (ADF) unit-root test. "
            "Returns STRUCT(statistic DOUBLE, p_value DOUBLE, lags BIGINT, "
            "is_stationary BOOLEAN, cv_1pct DOUBLE, cv_5pct DOUBLE, cv_10pct DOUBLE). "
            "Uses constant-only ('c') regression and AIC lag selection. "
            "p-values are approximate (MacKinnon 9-point lookup table). "
            "Returns NaN for series shorter than 4 observations.";
        desc.examples = {
            "ts_adf(LIST(y ORDER BY ds))",
            "ts_adf(LIST(y ORDER BY ds), 3)"
        };
        desc.categories = {"time-series", "diagnostics"};
        desc.parameter_names = {"series", "max_lags"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::INTEGER)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    // anofox_fcst_ts_adf alias (mirror bootstrap.cpp dual-registration pattern)
    child_list_t<LogicalType> struct_children2;
    struct_children2.push_back(make_pair("statistic",     LogicalType(LogicalTypeId::DOUBLE)));
    struct_children2.push_back(make_pair("p_value",       LogicalType(LogicalTypeId::DOUBLE)));
    struct_children2.push_back(make_pair("lags",          LogicalType(LogicalTypeId::BIGINT)));
    struct_children2.push_back(make_pair("is_stationary", LogicalType(LogicalTypeId::BOOLEAN)));
    struct_children2.push_back(make_pair("cv_1pct",       LogicalType(LogicalTypeId::DOUBLE)));
    struct_children2.push_back(make_pair("cv_5pct",       LogicalType(LogicalTypeId::DOUBLE)));
    struct_children2.push_back(make_pair("cv_10pct",      LogicalType(LogicalTypeId::DOUBLE)));
    auto result_type2 = LogicalType::STRUCT(std::move(struct_children2));

    ScalarFunctionSet anofox_set("anofox_fcst_ts_adf");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        result_type2,
        TsAdfFunction
    ));
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER)},
        result_type2,
        TsAdfFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_adf";
        FunctionDescription desc;
        desc.description =
            "Augmented Dickey-Fuller (ADF) unit-root test (prefixed alias). "
            "Returns STRUCT(statistic, p_value, lags, is_stationary, cv_1pct, cv_5pct, cv_10pct).";
        desc.examples = {"anofox_fcst_ts_adf(LIST(y ORDER BY ds))"};
        desc.categories = {"time-series", "diagnostics"};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_kpss(series LIST(DOUBLE) [, lags INTEGER]) → STRUCT(statistic, p_value,
//   lags, is_stationary, cv_1pct, cv_5pct, cv_10pct)   [STAT-02]
//
// Same 7-field STRUCT as ts_adf (KPSS returns the crate's StationarityResult).
// For KPSS, is_stationary=true means the statistic FAILS to reject the
// stationarity null (statistic below the 5% critical value).
// ============================================================================

static void TsKpssFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    UnifiedVectorFormat lags_data_fmt;
    bool has_lags = (args.ColumnCount() >= 2);
    if (has_lags) {
        args.data[1].ToUnifiedFormat(count, lags_data_fmt);
    }

    auto &struct_entries = StructVector::GetEntries(result);
    auto stat_data  = FlatVector::GetData<double>(*struct_entries[0]);
    auto pval_data  = FlatVector::GetData<double>(*struct_entries[1]);
    auto lags_data  = FlatVector::GetData<int64_t>(*struct_entries[2]);
    auto istat_data = FlatVector::GetData<bool>(*struct_entries[3]);
    auto cv1_data   = FlatVector::GetData<double>(*struct_entries[4]);
    auto cv5_data   = FlatVector::GetData<double>(*struct_entries[5]);
    auto cv10_data  = FlatVector::GetData<double>(*struct_entries[6]);

    vector<double> series;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        int32_t lags_val = -1;
        if (has_lags) {
            auto l_idx = lags_data_fmt.sel->get_index(row_idx);
            if (lags_data_fmt.validity.RowIsValid(l_idx)) {
                lags_val = UnifiedVectorFormat::GetData<int32_t>(lags_data_fmt)[l_idx];
            }
        }

        ExtractListAsDoubleLocal(values_vec, row_idx, series);

        AnofoxStationarityResult r = {};
        AnofoxError err = {};
        bool ok = anofox_ts_kpss(
            series.data(), nullptr, series.size(),
            static_cast<int>(lags_val), &r, &err);

        if (!ok) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        stat_data[row_idx]  = r.statistic;
        pval_data[row_idx]  = r.p_value;
        lags_data[row_idx]  = static_cast<int64_t>(r.lags);
        istat_data[row_idx] = r.is_stationary;
        cv1_data[row_idx]   = r.cv_1pct;
        cv5_data[row_idx]   = r.cv_5pct;
        cv10_data[row_idx]  = r.cv_10pct;
    }
}

// ============================================================================
// ts_stationarity(series LIST(DOUBLE)) → STRUCT(adf_statistic, adf_p_value,
//   kpss_statistic, kpss_p_value, adf_is_stationary, kpss_is_stationary,
//   verdict)   [STAT-03]
//
// Runs both ADF and KPSS and derives the four-way verdict
// (stationary / trend_stationary / difference_stationary / non_stationary).
// STRUCT field order must match AnofoxCombinedStationarityResult.
// ============================================================================

static void TsStationarityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    auto &struct_entries = StructVector::GetEntries(result);
    auto adf_stat_data  = FlatVector::GetData<double>(*struct_entries[0]);
    auto adf_pval_data  = FlatVector::GetData<double>(*struct_entries[1]);
    auto kpss_stat_data = FlatVector::GetData<double>(*struct_entries[2]);
    auto kpss_pval_data = FlatVector::GetData<double>(*struct_entries[3]);
    auto adf_istat_data = FlatVector::GetData<bool>(*struct_entries[4]);
    auto kpss_istat_data = FlatVector::GetData<bool>(*struct_entries[5]);
    auto &verdict_vec   = *struct_entries[6];
    auto verdict_data   = FlatVector::GetData<string_t>(verdict_vec);

    vector<double> series;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        ExtractListAsDoubleLocal(values_vec, row_idx, series);

        AnofoxCombinedStationarityResult r = {};
        AnofoxError err = {};
        bool ok = anofox_ts_stationarity(
            series.data(), nullptr, series.size(), &r, &err);

        if (!ok) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        adf_stat_data[row_idx]   = r.adf_statistic;
        adf_pval_data[row_idx]   = r.adf_p_value;
        kpss_stat_data[row_idx]  = r.kpss_statistic;
        kpss_pval_data[row_idx]  = r.kpss_p_value;
        adf_istat_data[row_idx]  = r.adf_is_stationary;
        kpss_istat_data[row_idx] = r.kpss_is_stationary;
        // r.verdict is a NUL-terminated char[32]
        verdict_data[row_idx]    = StringVector::AddString(verdict_vec, r.verdict);
    }
}

// ----------------------------------------------------------------------------

static child_list_t<LogicalType> StationarityStructChildren() {
    child_list_t<LogicalType> c;
    c.push_back(make_pair("statistic",     LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("p_value",       LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("lags",          LogicalType(LogicalTypeId::BIGINT)));
    c.push_back(make_pair("is_stationary", LogicalType(LogicalTypeId::BOOLEAN)));
    c.push_back(make_pair("cv_1pct",       LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("cv_5pct",       LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("cv_10pct",      LogicalType(LogicalTypeId::DOUBLE)));
    return c;
}

static child_list_t<LogicalType> CombinedStationarityStructChildren() {
    child_list_t<LogicalType> c;
    c.push_back(make_pair("adf_statistic",      LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("adf_p_value",        LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("kpss_statistic",     LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("kpss_p_value",       LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("adf_is_stationary",  LogicalType(LogicalTypeId::BOOLEAN)));
    c.push_back(make_pair("kpss_is_stationary", LogicalType(LogicalTypeId::BOOLEAN)));
    c.push_back(make_pair("verdict",            LogicalType(LogicalTypeId::VARCHAR)));
    return c;
}

void RegisterTsKpssFunction(ExtensionLoader &loader) {
    auto result_type = LogicalType::STRUCT(StationarityStructChildren());

    ScalarFunctionSet kpss_set("ts_kpss");
    kpss_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        result_type, TsKpssFunction));
    kpss_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER)},
        result_type, TsKpssFunction));
    {
        CreateScalarFunctionInfo info(kpss_set);
        FunctionDescription desc;
        desc.description =
            "Kwiatkowski-Phillips-Schmidt-Shin (KPSS) stationarity test. "
            "Null hypothesis: the series is level-stationary. "
            "Returns STRUCT(statistic DOUBLE, p_value DOUBLE, lags BIGINT, "
            "is_stationary BOOLEAN, cv_1pct DOUBLE, cv_5pct DOUBLE, cv_10pct DOUBLE); "
            "is_stationary=true means the null is NOT rejected at 5%. "
            "Uses level ('c') specification; p-values are approximate (interpolated, "
            "clamped to [0.01, 0.10]).";
        desc.examples = {"ts_kpss(LIST(y ORDER BY ds))", "ts_kpss(LIST(y ORDER BY ds), 4)"};
        desc.categories = {"time-series", "diagnostics"};
        desc.parameter_names = {"series", "lags"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::INTEGER)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet alias_set("anofox_fcst_ts_kpss");
    alias_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType::STRUCT(StationarityStructChildren()), TsKpssFunction));
    alias_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER)},
        LogicalType::STRUCT(StationarityStructChildren()), TsKpssFunction));
    {
        CreateScalarFunctionInfo info(alias_set);
        info.alias_of = "ts_kpss";
        FunctionDescription desc;
        desc.description = "KPSS stationarity test (prefixed alias).";
        desc.examples = {"anofox_fcst_ts_kpss(LIST(y ORDER BY ds))"};
        desc.categories = {"time-series", "diagnostics"};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

void RegisterTsStationarityFunction(ExtensionLoader &loader) {
    auto result_type = LogicalType::STRUCT(CombinedStationarityStructChildren());

    ScalarFunctionSet stat_set("ts_stationarity");
    stat_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        result_type, TsStationarityFunction));
    {
        CreateScalarFunctionInfo info(stat_set);
        FunctionDescription desc;
        desc.description =
            "Combined ADF + KPSS stationarity verdict. Runs both tests and returns "
            "STRUCT(adf_statistic DOUBLE, adf_p_value DOUBLE, kpss_statistic DOUBLE, "
            "kpss_p_value DOUBLE, adf_is_stationary BOOLEAN, kpss_is_stationary BOOLEAN, "
            "verdict VARCHAR). verdict is one of "
            "'stationary', 'trend_stationary', 'difference_stationary', 'non_stationary'.";
        desc.examples = {"ts_stationarity(LIST(y ORDER BY ds))"};
        desc.categories = {"time-series", "diagnostics"};
        desc.parameter_names = {"series"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet alias_set("anofox_fcst_ts_stationarity");
    alias_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType::STRUCT(CombinedStationarityStructChildren()), TsStationarityFunction));
    {
        CreateScalarFunctionInfo info(alias_set);
        info.alias_of = "ts_stationarity";
        FunctionDescription desc;
        desc.description = "Combined ADF + KPSS stationarity verdict (prefixed alias).";
        desc.examples = {"anofox_fcst_ts_stationarity(LIST(y ORDER BY ds))"};
        desc.categories = {"time-series", "diagnostics"};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

} // namespace duckdb
