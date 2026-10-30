#pragma once

// ============================================================================
// ts_changepoints_params.hpp -- shared params-MAP parser for
// ts_detect_changepoints_by (_ts_detect_changepoints_by_native) and
// ts_detect_changepoints_agg (#287, BUG-06, Phase 20 plan 20-06;
// deseasonalize/seasonal_period added for BUG-05, D1-b, Phase 20 plan
// 20-07).
//
// Before this header, ts_detect_changepoints_agg's "Parse params MAP if
// valid" block was empty (20-RESEARCH.md): every key, including
// hazard_lambda, was silently ignored, while ts_detect_changepoints_by read
// the same key through its own static regex-based string parser. The two
// consumers could disagree on identical inputs with identical params. This
// header is the single source of truth for the changepoint params key set
// (hazard_lambda, threshold, deseasonalize, seasonal_period) so they cannot
// drift again. Unknown keys are ignored, matching both consumers' prior
// behaviour.
//
// Code review WR-04 (2026-10-23): key-name matching below is an exact
// string match, so a TYPO'D KEY NAME (e.g. 'Threshold', 'treshold',
// 'seasonalPeriod') is indistinguishable from "key absent" and silently
// uses the default -- the same silent-drift failure mode that the
// value-level strictness on threshold/deseasonalize/seasonal_period was
// deliberately written to avoid, just one layer up (key name instead of
// key value). This is a KNOWN, DELIBERATE tradeoff: unknown-key leniency
// (needed for backward/forward params-MAP compatibility -- rejecting an
// unrecognized key would be a breaking change for any caller passing an
// extra key this header doesn't yet know about) is accepted here even
// though it reintroduces, at the key-name layer, the exact risk the
// value-level strictness exists to prevent. If key-typo protection is
// wanted later, it needs its own explicit decision (validate every input
// key against the four recognized names and throw naming the unrecognized
// key) -- not a silent addition, since it would change behavior for any
// caller currently passing an unrecognized key and expecting it ignored.
// ============================================================================

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb_compat.hpp"

#include <cctype>
#include <limits>

namespace duckdb {

// Parsed, validated params-MAP (or STRUCT) contents shared by both
// changepoint consumers. `threshold` is quiet-NaN when the key is absent
// (unset -- the default MAP run-length-reset heuristic applies); range
// validation ([0, 1], finiteness) happens in Rust
// (detect_changepoints_bocpd_with_options, D2-ii), not here. `deseasonalize`
// is 'auto' by default (#287): Rust deseasonalizes only a clearly seasonal
// series and otherwise runs on the raw values, byte-identical to
// `deseasonalize: false`. `true` always deseasonalizes, `false` never does.
// `seasonal_period` is 0 (= auto-detect) by default and is consulted when
// deseasonalizing (explicitly or by 'auto').
struct ChangepointParamSet {
    double hazard_lambda = 250.0;
    double threshold = std::numeric_limits<double>::quiet_NaN();
    bool deseasonalize = false;
    bool auto_deseasonalize = true;
    int64_t seasonal_period = 0;
};

// Reads hazard_lambda/threshold from the params Value. Any params form (MAP,
// STRUCT, VARIANT, MAP of VARIANT, the empty STRUCT {}) is first turned into
// MAP(VARCHAR, VARCHAR) by ParamsAsVarcharMap, so the reader below only knows
// MAPs. (#300 widened ts_detect_changepoints_agg's params to ANY and routes
// ts_detect_changepoints_by's macro through _ts_params_map, which supersedes
// code review WR-05's MAP-only note.) A NULL Value or a missing key falls
// back to the default. An
// unparsable hazard_lambda falls back to the default -- this mirrors the
// pre-existing leniency of ts_detect_changepoints_by's removed
// ParseHazardLambda helper (deferred-items.md entry 3: unparsable numeric
// params fall back to defaults on every MAP-param surface; kept here for
// consumer parity, not introduced by this header). A PRESENT but
// unparsable threshold throws InvalidInputException naming the key instead
// of silently falling back to "unset" -- a typo'd threshold silently
// disabling the feature would be a worse silent-drift bug than erroring.
inline ChangepointParamSet ParseChangepointParamSet(const Value &params_in) {
    ChangepointParamSet result;
    // Any params form (MAP, STRUCT, VARIANT, MAP of VARIANT) as the MAP(VARCHAR, VARCHAR)
    // the reader below handles; NULL stays NULL (defaults).
    const Value params_value = params_in.IsNull() ? params_in : ParamsAsVarcharMap(params_in);
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

    // deseasonalize (#287, BUG-05, D1-b): true/false/1/0/auto,
    // case-insensitive; 'auto' is the default. Unlike hazard_lambda, a
    // PRESENT but unparsable value throws naming the key instead of silently
    // falling back to the default, matching threshold's "typo must not
    // silently disable the feature" rule above.
    Value deseasonalize_val = read_key("deseasonalize");
    if (!deseasonalize_val.IsNull()) {
        string lowered = StringUtil::Lower(deseasonalize_val.ToString());
        if (lowered == "true" || lowered == "1") {
            result.deseasonalize = true;
            result.auto_deseasonalize = false;
        } else if (lowered == "false" || lowered == "0") {
            result.deseasonalize = false;
            result.auto_deseasonalize = false;
        } else if (lowered == "auto") {
            result.deseasonalize = false;
            result.auto_deseasonalize = true;
        } else {
            throw InvalidInputException(
                "ts_detect_changepoints: invalid 'deseasonalize' value '%s' -- must be true/false/1/0/auto",
                deseasonalize_val.ToString());
        }
    }

    // seasonal_period (#287, BUG-05, D1-b): strict non-negative integer.
    // Accepted without deseasonalize set (no effect in that case, per the
    // struct doc comment above) -- range/period-usability logic lives in
    // Rust (deseasonalize_for_changepoints).
    Value seasonal_period_val = read_key("seasonal_period");
    if (!seasonal_period_val.IsNull()) {
        string raw = seasonal_period_val.ToString();
        bool all_digits = !raw.empty();
        for (unsigned char c : raw) {
            if (!std::isdigit(c)) {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            try {
                result.seasonal_period = std::stoll(raw);
            } catch (...) {
                all_digits = false;
            }
        }
        if (!all_digits) {
            throw InvalidInputException(
                "ts_detect_changepoints: invalid 'seasonal_period' value '%s' -- must be a non-negative integer",
                raw);
        }
    }

    return result;
}

} // namespace duckdb
