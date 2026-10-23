#pragma once

// ============================================================================
// ts_changepoints_params.hpp -- shared params-MAP parser for
// ts_detect_changepoints_by (_ts_detect_changepoints_by_native) and
// ts_detect_changepoints_agg (#287, BUG-06, Phase 20 plan 20-06).
//
// Before this header, ts_detect_changepoints_agg's "Parse params MAP if
// valid" block was empty (20-RESEARCH.md): every key, including
// hazard_lambda, was silently ignored, while ts_detect_changepoints_by read
// the same key through its own static regex-based string parser. The two
// consumers could disagree on identical inputs with identical params. This
// header is the single source of truth for the changepoint params key set
// (hazard_lambda today; threshold from Task 2 onward) so they cannot drift
// again. Unknown keys are ignored, matching both consumers' prior behaviour.
// ============================================================================

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

namespace duckdb {

// Parsed, validated params-MAP (or STRUCT) contents shared by both
// changepoint consumers.
struct ChangepointParamSet {
    double hazard_lambda = 250.0;
};

// Reads hazard_lambda from a MAP(VARCHAR, VARCHAR) or STRUCT Value. A NULL
// Value or a missing key falls back to the default. An unparsable numeric
// value also falls back to the default -- this mirrors the pre-existing
// leniency of ts_detect_changepoints_by's removed ParseHazardLambda helper
// (deferred-items.md entry 3: unparsable numeric params fall back to
// defaults on every MAP-param surface; kept here for consumer parity, not
// introduced by this header).
inline ChangepointParamSet ParseChangepointParamSet(const Value &params_value) {
    ChangepointParamSet result;
    if (params_value.IsNull()) {
        return result;
    }

    auto read_key = [&](const string &key) -> Value {
        if (params_value.type().id() == LogicalTypeId::MAP) {
            auto &map_children = MapValue::GetChildren(params_value);
            for (auto &child : map_children) {
                auto &k = StructValue::GetChildren(child)[0];
                auto &v = StructValue::GetChildren(child)[1];
                if (k.ToString() == key) {
                    return v;
                }
            }
        } else if (params_value.type().id() == LogicalTypeId::STRUCT) {
            auto &struct_children = StructValue::GetChildren(params_value);
            auto &child_types = StructType::GetChildTypes(params_value.type());
            for (idx_t i = 0; i < child_types.size(); i++) {
                if (NameString(child_types[i].first) == key) {
                    return struct_children[i];
                }
            }
        }
        return Value();
    };

    Value hazard_val = read_key("hazard_lambda");
    if (!hazard_val.IsNull()) {
        try {
            result.hazard_lambda = std::stod(hazard_val.ToString());
        } catch (...) {
            // Unparsable -> default (see function doc comment above).
        }
    }

    return result;
}

} // namespace duckdb
