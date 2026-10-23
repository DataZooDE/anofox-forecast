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
// (hazard_lambda, threshold) so they cannot drift again. Unknown keys are
// ignored, matching both consumers' prior behaviour.
// ============================================================================

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include <limits>

namespace duckdb {

// Parsed, validated params-MAP (or STRUCT) contents shared by both
// changepoint consumers. `threshold` is quiet-NaN when the key is absent
// (unset -- the default MAP run-length-reset heuristic applies); range
// validation ([0, 1], finiteness) happens in Rust
// (detect_changepoints_bocpd_with_options, D2-ii), not here.
struct ChangepointParamSet {
    double hazard_lambda = 250.0;
    double threshold = std::numeric_limits<double>::quiet_NaN();
};

// Reads hazard_lambda/threshold from a MAP(VARCHAR, VARCHAR) or STRUCT
// Value. A NULL Value or a missing key falls back to the default. An
// unparsable hazard_lambda falls back to the default -- this mirrors the
// pre-existing leniency of ts_detect_changepoints_by's removed
// ParseHazardLambda helper (deferred-items.md entry 3: unparsable numeric
// params fall back to defaults on every MAP-param surface; kept here for
// consumer parity, not introduced by this header). A PRESENT but
// unparsable threshold throws InvalidInputException naming the key instead
// of silently falling back to "unset" -- a typo'd threshold silently
// disabling the feature would be a worse silent-drift bug than erroring.
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

    Value threshold_val = read_key("threshold");
    if (!threshold_val.IsNull()) {
        try {
            result.threshold = std::stod(threshold_val.ToString());
        } catch (...) {
            throw InvalidInputException(
                "ts_detect_changepoints: invalid 'threshold' value '%s' -- must be a number in [0, 1]",
                threshold_val.ToString());
        }
    }

    return result;
}

} // namespace duckdb
