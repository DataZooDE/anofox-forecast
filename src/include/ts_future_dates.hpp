#pragma once

// D-12 calendar rule marshalling (plan 12-05): maps a frequency spec to the
// (step_months, step_micros) pair anofox_ts_future_dates expects, and calls
// it into a horizon-sized buffer. The calendar rule itself (month-end /
// day-of-month-clamp, deterministic frequency detection) lives in Rust
// (crates/anofox-fcst-core/src/calendar.rs) — this header only marshals.

#include "duckdb.hpp"
#include "anofox_fcst_ffi.h"
#include "ts_fill_gaps_native.hpp" // DateColumnType, ParsedFrequency

namespace duckdb {

// Map a (frequency type, raw seconds value) spec to the (step_months,
// step_micros) pair anofox_ts_future_dates expects. Exactly mirrors the
// arithmetic ts_forecast_scalar.cpp's old ComputeForecastDate used
// (:188-230): INTEGER/BIGINT date columns always treat freq_seconds as a
// raw integer step; MONTHLY/QUARTERLY/YEARLY multiply by 1/3/12; FIXED on a
// DATE/TIMESTAMP column converts freq_seconds to microseconds (raw days or
// plain seconds per freq_is_raw).
inline void StepFromFrequency(FrequencyType type, int64_t freq_seconds, bool freq_is_raw,
                               DateColumnType date_type, int64_t &step_months, int64_t &step_micros) {
    step_months = 0;
    step_micros = 0;

    if (type == FrequencyType::MONTHLY) {
        step_months = freq_seconds;
        return;
    }
    if (type == FrequencyType::QUARTERLY) {
        step_months = freq_seconds * 3;
        return;
    }
    if (type == FrequencyType::YEARLY) {
        step_months = freq_seconds * 12;
        return;
    }

    // FrequencyType::FIXED
    if (date_type == DateColumnType::INTEGER || date_type == DateColumnType::BIGINT) {
        step_micros = freq_seconds;
    } else {
        step_micros = freq_is_raw ? freq_seconds * 86400LL * 1000000LL : freq_seconds * 1000000LL;
    }
}

// Call anofox_ts_future_dates into a horizon-sized buffer. `observed` must
// be non-empty. Throws InvalidInputException on any Rust-reported error
// (invalid steps, empty input, overflow) — never returns a partially
// written buffer.
inline vector<int64_t> ComputeFutureDates(const vector<int64_t> &observed, idx_t horizon, int64_t step_months,
                                           int64_t step_micros) {
    vector<int64_t> out_dates(horizon);
    if (horizon == 0) {
        return out_dates;
    }

    AnofoxError error;
    bool success = anofox_ts_future_dates(observed.data(), observed.size(), horizon, step_months, step_micros,
                                           out_dates.data(), &error);
    if (!success) {
        throw InvalidInputException(string(error.message));
    }
    return out_dates;
}

} // namespace duckdb
