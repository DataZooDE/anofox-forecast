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

} // namespace duckdb
