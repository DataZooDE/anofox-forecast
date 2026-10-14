/// Statistical diagnostic scalar functions for the anofox-forecast DuckDB extension.
///
/// This file implements:
///   - ts_adf(series LIST(DOUBLE) [, max_lags INTEGER [, regression VARCHAR]]) → STRUCT(...)
///   - RegisterTsAdfFunction(ExtensionLoader&)
///
/// It follows the STRUCT-return pattern established in bootstrap.cpp and
/// reads its LIST(DOUBLE) arguments via ts_list_marshal.hpp's
/// ListDoubleReader / ScalarArgReader (Phase 12 D-01), not the deleted
/// per-file ExtractListAsDouble helper.
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
#include "ts_list_marshal.hpp"

namespace duckdb {

// List extraction for every function below goes through ts_list_marshal.hpp's
// ListDoubleReader::ReadSingleSeries (Phase 12 D-01/D-03/D-04, plan 12-02):
// the series is NULL-free after the Rust single-series gate
// (anofox_ts_series_gate) trims leading/trailing NULLs and errors on any
// interior NULL naming its 1-based position; the diagnostic FFIs below always
// receive `nullptr` for `validity` since the slice is guaranteed NULL-free.

// ============================================================================
// ts_adf(series LIST(DOUBLE)) → STRUCT(statistic, p_value, lags, is_stationary,
//                                      cv_1pct, cv_5pct, cv_10pct)
// ts_adf(series LIST(DOUBLE), max_lags INTEGER) → same STRUCT
// ts_adf(series LIST(DOUBLE), max_lags INTEGER, regression VARCHAR) → same STRUCT (D-05)
//
// The STRUCT field order must match RegisterTsAdfFunction's child_list_t order
// and the AnofoxStationarityResult field layout in anofox_fcst_ffi.h.
// ============================================================================

static void TsAdfFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    ListDoubleReader series_reader(values_vec, count);

    // Optional second argument: max_lags INTEGER (default -1 → auto)
    bool has_max_lags = (args.ColumnCount() >= 2);
    unique_ptr<ScalarArgReader<int32_t>> max_lags_reader;
    if (has_max_lags) {
        max_lags_reader = make_uniq<ScalarArgReader<int32_t>>(args.data[1], count);
    }

    // Optional third argument: regression VARCHAR ('c' default, or 'ct') (D-05)
    bool has_regression = (args.ColumnCount() >= 3);
    unique_ptr<ScalarArgReader<string_t>> regression_reader;
    if (has_regression) {
        regression_reader = make_uniq<ScalarArgReader<string_t>>(args.data[2], count);
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
        // NULL list, or nothing left after the Rust series gate trims
        // leading/trailing NULLs (D-04) → NULL STRUCT. Interior NULLs throw
        // InvalidInputException naming the 1-based position (D-03).
        if (!series_reader.ReadSingleSeries(row_idx, series, "ts_adf")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Resolve max_lags for this row (-1 = auto)
        int32_t max_lags_val = -1;
        if (has_max_lags && !max_lags_reader->IsNull(row_idx)) {
            max_lags_val = max_lags_reader->Get(row_idx);
        }

        // Resolve regression for this row (NULL/absent -> nullptr -> 'c')
        std::string regression_owned;
        const char *regression_str = nullptr;
        if (has_regression && !regression_reader->IsNull(row_idx)) {
            regression_owned = regression_reader->Get(row_idx).GetString();
            regression_str = regression_owned.c_str();
        }

        AnofoxStationarityResult r = {};
        AnofoxError err = {};
        bool ok = anofox_ts_adf(
            series.data(),
            /* validity= */ nullptr,   // NULL-free after the Rust series gate
            series.size(),
            static_cast<int>(max_lags_val),
            regression_str,
            &r, &err
        );

        if (!ok) {
            if (err.code == INVALID_INPUT) {
                throw InvalidInputException("ts_adf: %s", err.message ? err.message : "invalid input");
            }
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
    // 2-arg overload: ts_adf(series, max_lags). SPECIAL_HANDLING: a NULL
    // max_lags argument is a documented valid "auto" call, not a reason to
    // short-circuit the whole row to SQL NULL before TsAdfFunction runs
    // (mirrors conformal.cpp's ts_conformal_intervals fix).
    ScalarFunction adf_2arg(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER)},
        result_type,
        TsAdfFunction
    );
    adf_2arg.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    adf_set.AddFunction(adf_2arg);
    // 3-arg overload: ts_adf(series, max_lags, regression) (D-05). Same
    // SPECIAL_HANDLING reason: a NULL regression means 'c', not SQL NULL.
    ScalarFunction adf_3arg(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER),
         LogicalType(LogicalTypeId::VARCHAR)},
        result_type,
        TsAdfFunction
    );
    adf_3arg.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    adf_set.AddFunction(adf_3arg);
    {
        CreateScalarFunctionInfo info(adf_set);
        FunctionDescription desc;
        desc.description =
            "Augmented Dickey-Fuller (ADF) unit-root test, matching statsmodels "
            "adfuller(autolag='AIC'). Returns STRUCT(statistic DOUBLE, p_value DOUBLE, "
            "lags BIGINT, is_stationary BOOLEAN, cv_1pct DOUBLE, cv_5pct DOUBLE, "
            "cv_10pct DOUBLE). AIC lag selection over 0..max_lags; default "
            "max_lags = floor((n-1)^(1/3)). regression is 'c' (constant, default) or "
            "'ct' (constant + trend); NULL means 'c'. p-values are MacKinnon (1994) "
            "and critical values are MacKinnon (2010) finite-sample surfaces. "
            "Returns NaN statistic/p_value/critical values for series shorter than "
            "4 observations or an exactly-collinear regression.";
        desc.examples = {
            "ts_adf(LIST(y ORDER BY ds))",
            "ts_adf(LIST(y ORDER BY ds), 3)",
            "ts_adf(LIST(y ORDER BY ds), -1, 'ct')"
        };
        desc.categories = {"time-series", "diagnostics"};
        desc.parameter_names = {"series", "max_lags", "regression"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::INTEGER),
            LogicalType(LogicalTypeId::VARCHAR)
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
    ScalarFunction anofox_adf_2arg(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER)},
        result_type2,
        TsAdfFunction
    );
    anofox_adf_2arg.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    anofox_set.AddFunction(anofox_adf_2arg);
    ScalarFunction anofox_adf_3arg(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER),
         LogicalType(LogicalTypeId::VARCHAR)},
        result_type2,
        TsAdfFunction
    );
    anofox_adf_3arg.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    anofox_set.AddFunction(anofox_adf_3arg);
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
// ts_kpss(series LIST(DOUBLE) [, lags INTEGER [, regression VARCHAR]]) →
//   STRUCT(statistic, p_value, lags, is_stationary, cv_1pct, cv_5pct,
//   cv_10pct)   [STAT-02, D-05a]
//
// Same 7-field STRUCT as ts_adf (KPSS returns the crate's StationarityResult
// for 'c', or the extension's own trend KPSS for 'ct' -- D-05a).
// For KPSS, is_stationary=true means the statistic FAILS to reject the
// stationarity null (statistic below the 5% critical value).
// ============================================================================

static void TsKpssFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    ListDoubleReader series_reader(values_vec, count);

    bool has_lags = (args.ColumnCount() >= 2);
    unique_ptr<ScalarArgReader<int32_t>> lags_reader;
    if (has_lags) {
        lags_reader = make_uniq<ScalarArgReader<int32_t>>(args.data[1], count);
    }

    // Optional third argument: regression VARCHAR ('c' default, or 'ct') (D-05a)
    bool has_regression = (args.ColumnCount() >= 3);
    unique_ptr<ScalarArgReader<string_t>> regression_reader;
    if (has_regression) {
        regression_reader = make_uniq<ScalarArgReader<string_t>>(args.data[2], count);
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
        if (!series_reader.ReadSingleSeries(row_idx, series, "ts_kpss")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        int32_t lags_val = -1;
        if (has_lags && !lags_reader->IsNull(row_idx)) {
            lags_val = lags_reader->Get(row_idx);
        }

        std::string regression_owned;
        const char *regression_str = nullptr;
        if (has_regression && !regression_reader->IsNull(row_idx)) {
            regression_owned = regression_reader->Get(row_idx).GetString();
            regression_str = regression_owned.c_str();
        }

        AnofoxStationarityResult r = {};
        AnofoxError err = {};
        bool ok = anofox_ts_kpss(
            series.data(), nullptr, series.size(),
            static_cast<int>(lags_val), regression_str, &r, &err);

        if (!ok) {
            if (err.code == INVALID_INPUT) {
                throw InvalidInputException("ts_kpss: %s", err.message ? err.message : "invalid input");
            }
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
// ts_stationarity(series LIST(DOUBLE) [, regression VARCHAR]) →
//   STRUCT(adf_statistic, adf_p_value, kpss_statistic, kpss_p_value,
//   adf_is_stationary, kpss_is_stationary, verdict)   [STAT-03, D-04, D-05]
//
// Runs both ADF and KPSS (same regression applied to both) and derives the
// D-04 four-way verdict (stationary / non_stationary / trend_stationary /
// difference_stationary), or 'constant', or SQL NULL (verdict child only)
// for a degenerate series.
// STRUCT field order must match AnofoxCombinedStationarityResult.
// ============================================================================

static void TsStationarityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    ListDoubleReader series_reader(values_vec, count);

    // Optional second argument: regression VARCHAR ('c' default, or 'ct') (D-04/D-05)
    bool has_regression = (args.ColumnCount() >= 2);
    unique_ptr<ScalarArgReader<string_t>> regression_reader;
    if (has_regression) {
        regression_reader = make_uniq<ScalarArgReader<string_t>>(args.data[1], count);
    }

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
        if (!series_reader.ReadSingleSeries(row_idx, series, "ts_stationarity")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        std::string regression_owned;
        const char *regression_str = nullptr;
        if (has_regression && !regression_reader->IsNull(row_idx)) {
            regression_owned = regression_reader->Get(row_idx).GetString();
            regression_str = regression_owned.c_str();
        }

        AnofoxCombinedStationarityResult r = {};
        AnofoxError err = {};
        bool ok = anofox_ts_stationarity(
            series.data(), nullptr, series.size(), regression_str, &r, &err);

        if (!ok) {
            if (err.code == INVALID_INPUT) {
                throw InvalidInputException("ts_stationarity: %s", err.message ? err.message : "invalid input");
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // D-04: an empty verdict buffer (NUL at index 0) means SQL NULL --
        // set only the verdict child NULL, every other field stays valid
        // (the per-test adf_*/kpss_* fields always reflect the real result).
        if (r.verdict[0] == '\0') {
            FlatVector::SetNull(verdict_vec, row_idx, true);
        } else {
            verdict_data[row_idx] = StringVector::AddString(verdict_vec, r.verdict);
        }

        adf_stat_data[row_idx]   = r.adf_statistic;
        adf_pval_data[row_idx]   = r.adf_p_value;
        kpss_stat_data[row_idx]  = r.kpss_statistic;
        kpss_pval_data[row_idx]  = r.kpss_p_value;
        adf_istat_data[row_idx]  = r.adf_is_stationary;
        kpss_istat_data[row_idx] = r.kpss_is_stationary;
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
    // 2-arg and 3-arg overloads take a nullable lags / regression: a NULL
    // means "auto" / 'c' respectively, not SQL NULL (same SPECIAL_HANDLING
    // fix as ts_adf).
    ScalarFunction kpss_2arg(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER)},
        result_type, TsKpssFunction);
    kpss_2arg.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    kpss_set.AddFunction(kpss_2arg);
    ScalarFunction kpss_3arg(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER),
         LogicalType(LogicalTypeId::VARCHAR)},
        result_type, TsKpssFunction);
    kpss_3arg.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    kpss_set.AddFunction(kpss_3arg);
    {
        CreateScalarFunctionInfo info(kpss_set);
        FunctionDescription desc;
        desc.description =
            "Kwiatkowski-Phillips-Schmidt-Shin (KPSS) stationarity test, matching "
            "tseries::kpss.test(lshort = TRUE). Returns STRUCT(statistic DOUBLE, "
            "p_value DOUBLE, lags BIGINT, is_stationary BOOLEAN, cv_1pct DOUBLE, "
            "cv_5pct DOUBLE, cv_10pct DOUBLE); is_stationary=true means the null "
            "is NOT rejected at 5%. regression is 'c' (level, null = \"Level\", "
            "default) or 'ct' (trend, null = \"Trend\"); NULL means 'c'. "
            "p-values are approximate (interpolated, clamped to [0.01, 0.10]). "
            "Returns NaN statistic/p_value/critical values for series shorter "
            "than 4 observations or (under 'ct') an exactly-collinear trend fit.";
        desc.examples = {
            "ts_kpss(LIST(y ORDER BY ds))",
            "ts_kpss(LIST(y ORDER BY ds), 4)",
            "ts_kpss(LIST(y ORDER BY ds), -1, 'ct')"
        };
        desc.categories = {"time-series", "diagnostics"};
        desc.parameter_names = {"series", "lags", "regression"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::INTEGER),
            LogicalType(LogicalTypeId::VARCHAR)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet alias_set("anofox_fcst_ts_kpss");
    alias_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType::STRUCT(StationarityStructChildren()), TsKpssFunction));
    ScalarFunction alias_kpss_2arg(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER)},
        LogicalType::STRUCT(StationarityStructChildren()), TsKpssFunction);
    alias_kpss_2arg.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    alias_set.AddFunction(alias_kpss_2arg);
    ScalarFunction alias_kpss_3arg(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER),
         LogicalType(LogicalTypeId::VARCHAR)},
        LogicalType::STRUCT(StationarityStructChildren()), TsKpssFunction);
    alias_kpss_3arg.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    alias_set.AddFunction(alias_kpss_3arg);
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
    // 2-arg overload takes a nullable regression: NULL means 'c' (same
    // SPECIAL_HANDLING fix as ts_adf/ts_kpss).
    ScalarFunction stat_2arg(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::VARCHAR)},
        result_type, TsStationarityFunction);
    stat_2arg.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    stat_set.AddFunction(stat_2arg);
    {
        CreateScalarFunctionInfo info(stat_set);
        FunctionDescription desc;
        desc.description =
            "Combined ADF + KPSS stationarity verdict (D-04). Runs both tests "
            "(with the SAME regression applied to both) and returns "
            "STRUCT(adf_statistic DOUBLE, adf_p_value DOUBLE, kpss_statistic DOUBLE, "
            "kpss_p_value DOUBLE, adf_is_stationary BOOLEAN, kpss_is_stationary BOOLEAN, "
            "verdict VARCHAR). verdict is one of 'stationary' (ADF rejects, KPSS "
            "does not), 'non_stationary' (ADF does not reject, KPSS rejects), "
            "'trend_stationary' (neither rejects), 'difference_stationary' (both "
            "reject), or 'constant' (n >= 2, all values equal); verdict is SQL "
            "NULL when either test's statistic is NaN (too-short series, or an "
            "exactly-collinear regression). regression is 'c' (default) or 'ct'; "
            "NULL means 'c'.";
        desc.examples = {
            "ts_stationarity(LIST(y ORDER BY ds))",
            "ts_stationarity(LIST(y ORDER BY ds), 'ct')"
        };
        desc.categories = {"time-series", "diagnostics"};
        desc.parameter_names = {"series", "regression"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::VARCHAR)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet alias_set("anofox_fcst_ts_stationarity");
    alias_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType::STRUCT(CombinedStationarityStructChildren()), TsStationarityFunction));
    ScalarFunction alias_stat_2arg(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::VARCHAR)},
        LogicalType::STRUCT(CombinedStationarityStructChildren()), TsStationarityFunction);
    alias_stat_2arg.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    alias_set.AddFunction(alias_stat_2arg);
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

// ============================================================================
// ts_ljung_box(residuals LIST(DOUBLE) [, lags INTEGER]) → STRUCT(...)  [RESID-01]
// ============================================================================

static void TsLjungBoxFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    ListDoubleReader series_reader(values_vec, count);

    bool has_lags = (args.ColumnCount() >= 2);
    unique_ptr<ScalarArgReader<int32_t>> lags_reader;
    if (has_lags) {
        lags_reader = make_uniq<ScalarArgReader<int32_t>>(args.data[1], count);
    }

    auto &e = StructVector::GetEntries(result);
    auto stat_data = FlatVector::GetData<double>(*e[0]);
    auto pval_data = FlatVector::GetData<double>(*e[1]);
    auto lags_data = FlatVector::GetData<int64_t>(*e[2]);
    auto df_data   = FlatVector::GetData<int64_t>(*e[3]);

    vector<double> series;
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (!series_reader.ReadSingleSeries(row_idx, series, "ts_ljung_box")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }
        int32_t lags_val = -1;
        if (has_lags && !lags_reader->IsNull(row_idx)) {
            lags_val = lags_reader->Get(row_idx);
        }
        AnofoxLjungBoxResult r = {};
        AnofoxError err = {};
        if (!anofox_ts_ljung_box(series.data(), nullptr, series.size(),
                                 static_cast<int>(lags_val), &r, &err)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }
        stat_data[row_idx] = r.statistic;
        pval_data[row_idx] = r.p_value;
        lags_data[row_idx] = static_cast<int64_t>(r.lags);
        df_data[row_idx]   = static_cast<int64_t>(r.df);
    }
}

// ============================================================================
// ts_durbin_watson(residuals LIST(DOUBLE)) → STRUCT(statistic, interpretation)  [RESID-02]
// ============================================================================

static void TsDurbinWatsonFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();
    ListDoubleReader series_reader(values_vec, count);
    auto &e = StructVector::GetEntries(result);
    auto stat_data = FlatVector::GetData<double>(*e[0]);
    auto &interp_vec = *e[1];
    auto interp_data = FlatVector::GetData<string_t>(interp_vec);

    vector<double> series;
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (!series_reader.ReadSingleSeries(row_idx, series, "ts_durbin_watson")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }
        AnofoxDurbinWatsonResult r = {};
        AnofoxError err = {};
        if (!anofox_ts_durbin_watson(series.data(), nullptr, series.size(), &r, &err)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }
        stat_data[row_idx]   = r.statistic;
        interp_data[row_idx] = StringVector::AddString(interp_vec, r.interpretation);
    }
}

// ============================================================================
// ts_jarque_bera(residuals LIST(DOUBLE)) → STRUCT(statistic, p_value, skewness,
//   excess_kurtosis)  [RESID-03]
// ============================================================================

static void TsJarqueBeraFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();
    ListDoubleReader series_reader(values_vec, count);
    auto &e = StructVector::GetEntries(result);
    auto stat_data = FlatVector::GetData<double>(*e[0]);
    auto pval_data = FlatVector::GetData<double>(*e[1]);
    auto skew_data = FlatVector::GetData<double>(*e[2]);
    auto kurt_data = FlatVector::GetData<double>(*e[3]);

    vector<double> series;
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (!series_reader.ReadSingleSeries(row_idx, series, "ts_jarque_bera")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }
        AnofoxJarqueBeraResult r = {};
        AnofoxError err = {};
        if (!anofox_ts_jarque_bera(series.data(), nullptr, series.size(), &r, &err)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }
        stat_data[row_idx] = r.statistic;
        pval_data[row_idx] = r.p_value;
        skew_data[row_idx] = r.skewness;
        kurt_data[row_idx] = r.excess_kurtosis;
    }
}

// ============================================================================
// ts_residual_diagnostics(residuals LIST(DOUBLE) [, alpha DOUBLE]) → STRUCT(...)  [RESID-04]
// ============================================================================

static void TsResidualDiagnosticsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();

    ListDoubleReader series_reader(values_vec, count);

    bool has_alpha = (args.ColumnCount() >= 2);
    unique_ptr<ScalarArgReader<double>> alpha_reader;
    if (has_alpha) {
        alpha_reader = make_uniq<ScalarArgReader<double>>(args.data[1], count);
    }

    auto &e = StructVector::GetEntries(result);
    auto lb_stat = FlatVector::GetData<double>(*e[0]);
    auto lb_pval = FlatVector::GetData<double>(*e[1]);
    auto lb_lags = FlatVector::GetData<int64_t>(*e[2]);
    auto dw_stat = FlatVector::GetData<double>(*e[3]);
    auto &dw_interp_vec = *e[4];
    auto dw_interp = FlatVector::GetData<string_t>(dw_interp_vec);
    auto jb_stat = FlatVector::GetData<double>(*e[5]);
    auto jb_pval = FlatVector::GetData<double>(*e[6]);
    auto jb_skew = FlatVector::GetData<double>(*e[7]);
    auto jb_kurt = FlatVector::GetData<double>(*e[8]);
    auto adequate_data = FlatVector::GetData<bool>(*e[9]);

    vector<double> series;
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (!series_reader.ReadSingleSeries(row_idx, series, "ts_residual_diagnostics")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }
        double alpha_val = 0.05;
        if (has_alpha && !alpha_reader->IsNull(row_idx)) {
            alpha_val = alpha_reader->Get(row_idx);
        }
        AnofoxResidualDiagnosticsResult r = {};
        AnofoxError err = {};
        if (!anofox_ts_residual_diagnostics(series.data(), nullptr, series.size(),
                                            alpha_val, &r, &err)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }
        lb_stat[row_idx] = r.lb_statistic;
        lb_pval[row_idx] = r.lb_p_value;
        lb_lags[row_idx] = static_cast<int64_t>(r.lb_lags);
        dw_stat[row_idx] = r.dw_statistic;
        dw_interp[row_idx] = StringVector::AddString(dw_interp_vec, r.dw_interpretation);
        jb_stat[row_idx] = r.jb_statistic;
        jb_pval[row_idx] = r.jb_p_value;
        jb_skew[row_idx] = r.jb_skewness;
        jb_kurt[row_idx] = r.jb_excess_kurtosis;
        adequate_data[row_idx] = r.adequate;
    }
}

// ---------------------------------------------------------------------------
// Registration helpers for residual diagnostics
// ---------------------------------------------------------------------------

static child_list_t<LogicalType> LjungBoxStructChildren() {
    child_list_t<LogicalType> c;
    c.push_back(make_pair("statistic", LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("p_value",   LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("lags",      LogicalType(LogicalTypeId::BIGINT)));
    c.push_back(make_pair("df",        LogicalType(LogicalTypeId::BIGINT)));
    return c;
}

static child_list_t<LogicalType> DurbinWatsonStructChildren() {
    child_list_t<LogicalType> c;
    c.push_back(make_pair("statistic",      LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("interpretation", LogicalType(LogicalTypeId::VARCHAR)));
    return c;
}

static child_list_t<LogicalType> JarqueBeraStructChildren() {
    child_list_t<LogicalType> c;
    c.push_back(make_pair("statistic",       LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("p_value",         LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("skewness",        LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("excess_kurtosis", LogicalType(LogicalTypeId::DOUBLE)));
    return c;
}

static child_list_t<LogicalType> ResidualDiagnosticsStructChildren() {
    child_list_t<LogicalType> c;
    c.push_back(make_pair("lb_statistic",      LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("lb_p_value",        LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("lb_lags",           LogicalType(LogicalTypeId::BIGINT)));
    c.push_back(make_pair("dw_statistic",      LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("dw_interpretation", LogicalType(LogicalTypeId::VARCHAR)));
    c.push_back(make_pair("jb_statistic",      LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("jb_p_value",        LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("jb_skewness",       LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("jb_excess_kurtosis", LogicalType(LogicalTypeId::DOUBLE)));
    c.push_back(make_pair("adequate",          LogicalType(LogicalTypeId::BOOLEAN)));
    return c;
}

static void RegisterSimpleDiag(ExtensionLoader &loader, const string &name,
                               const string &alias_name, ScalarFunction fn1,
                               ScalarFunction fn1_alias, const string &desc_text,
                               const string &example) {
    ScalarFunctionSet s(name);
    s.AddFunction(fn1);
    {
        CreateScalarFunctionInfo info(s);
        FunctionDescription desc;
        desc.description = desc_text;
        desc.examples = {example};
        desc.categories = {"time-series", "diagnostics"};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
    ScalarFunctionSet a(alias_name);
    a.AddFunction(fn1_alias);
    {
        CreateScalarFunctionInfo info(a);
        info.alias_of = name;
        FunctionDescription desc;
        desc.description = desc_text + " (prefixed alias).";
        desc.categories = {"time-series", "diagnostics"};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

void RegisterTsLjungBoxFunction(ExtensionLoader &loader) {
    auto rt = LogicalType::STRUCT(LjungBoxStructChildren());
    ScalarFunctionSet s("ts_ljung_box");
    s.AddFunction(ScalarFunction({LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))}, rt, TsLjungBoxFunction));
    s.AddFunction(ScalarFunction({LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
                                  LogicalType(LogicalTypeId::INTEGER)}, rt, TsLjungBoxFunction));
    {
        CreateScalarFunctionInfo info(s);
        FunctionDescription desc;
        desc.description =
            "Ljung-Box white-noise test on residuals. Returns STRUCT(statistic DOUBLE, "
            "p_value DOUBLE, lags BIGINT, df BIGINT). Default lags = min(10, n/5); a large "
            "statistic / small p-value indicates residual autocorrelation.";
        desc.examples = {"ts_ljung_box(LIST(resid ORDER BY ds))", "ts_ljung_box(LIST(resid ORDER BY ds), 12)"};
        desc.categories = {"time-series", "diagnostics"};
        desc.parameter_names = {"residuals", "lags"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
                                LogicalType(LogicalTypeId::INTEGER)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
    auto rt2 = LogicalType::STRUCT(LjungBoxStructChildren());
    ScalarFunctionSet a("anofox_fcst_ts_ljung_box");
    a.AddFunction(ScalarFunction({LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))}, rt2, TsLjungBoxFunction));
    a.AddFunction(ScalarFunction({LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
                                  LogicalType(LogicalTypeId::INTEGER)}, rt2, TsLjungBoxFunction));
    {
        CreateScalarFunctionInfo info(a);
        info.alias_of = "ts_ljung_box";
        FunctionDescription desc;
        desc.description = "Ljung-Box white-noise test on residuals (prefixed alias).";
        desc.categories = {"time-series", "diagnostics"};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

void RegisterTsDurbinWatsonFunction(ExtensionLoader &loader) {
    RegisterSimpleDiag(
        loader, "ts_durbin_watson", "anofox_fcst_ts_durbin_watson",
        ScalarFunction({LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
                       LogicalType::STRUCT(DurbinWatsonStructChildren()), TsDurbinWatsonFunction),
        ScalarFunction({LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
                       LogicalType::STRUCT(DurbinWatsonStructChildren()), TsDurbinWatsonFunction),
        "Durbin-Watson first-order autocorrelation statistic on residuals. Returns "
        "STRUCT(statistic DOUBLE, interpretation VARCHAR); statistic in [0,4], ~2 means no "
        "autocorrelation. interpretation is one of positive_strong / positive_weak / none / "
        "negative_weak / negative_strong.",
        "ts_durbin_watson(LIST(resid ORDER BY ds))");
}

void RegisterTsJarqueBeraFunction(ExtensionLoader &loader) {
    RegisterSimpleDiag(
        loader, "ts_jarque_bera", "anofox_fcst_ts_jarque_bera",
        ScalarFunction({LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
                       LogicalType::STRUCT(JarqueBeraStructChildren()), TsJarqueBeraFunction),
        ScalarFunction({LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
                       LogicalType::STRUCT(JarqueBeraStructChildren()), TsJarqueBeraFunction),
        "Jarque-Bera normality test on residuals. Returns STRUCT(statistic DOUBLE, "
        "p_value DOUBLE, skewness DOUBLE, excess_kurtosis DOUBLE). A small p-value rejects "
        "normality.",
        "ts_jarque_bera(LIST(resid ORDER BY ds))");
}

void RegisterTsResidualDiagnosticsFunction(ExtensionLoader &loader) {
    auto rt = LogicalType::STRUCT(ResidualDiagnosticsStructChildren());
    ScalarFunctionSet s("ts_residual_diagnostics");
    s.AddFunction(ScalarFunction({LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))}, rt, TsResidualDiagnosticsFunction));
    s.AddFunction(ScalarFunction({LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
                                  LogicalType(LogicalTypeId::DOUBLE)}, rt, TsResidualDiagnosticsFunction));
    {
        CreateScalarFunctionInfo info(s);
        FunctionDescription desc;
        desc.description =
            "Combined residual adequacy report: Ljung-Box + Durbin-Watson + Jarque-Bera. "
            "Returns STRUCT(lb_statistic, lb_p_value, lb_lags, dw_statistic, dw_interpretation, "
            "jb_statistic, jb_p_value, jb_skewness, jb_excess_kurtosis, adequate BOOLEAN). "
            "adequate = (lb_p_value > alpha); alpha defaults to 0.05. Jarque-Bera and "
            "Durbin-Watson are advisory and do not affect the adequacy verdict.";
        desc.examples = {"ts_residual_diagnostics(LIST(resid ORDER BY ds))",
                         "ts_residual_diagnostics(LIST(resid ORDER BY ds), 0.01)"};
        desc.categories = {"time-series", "diagnostics"};
        desc.parameter_names = {"residuals", "alpha"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
                                LogicalType(LogicalTypeId::DOUBLE)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
    auto rt2 = LogicalType::STRUCT(ResidualDiagnosticsStructChildren());
    ScalarFunctionSet a("anofox_fcst_ts_residual_diagnostics");
    a.AddFunction(ScalarFunction({LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))}, rt2, TsResidualDiagnosticsFunction));
    a.AddFunction(ScalarFunction({LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
                                  LogicalType(LogicalTypeId::DOUBLE)}, rt2, TsResidualDiagnosticsFunction));
    {
        CreateScalarFunctionInfo info(a);
        info.alias_of = "ts_residual_diagnostics";
        FunctionDescription desc;
        desc.description = "Combined residual adequacy report (prefixed alias).";
        desc.categories = {"time-series", "diagnostics"};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

} // namespace duckdb
