#pragma once

// ============================================================================
// TsCollectFinalizeBarrier - shared "collect in InOut, compute in final"
// barrier for table in-out functions (#285).
//
// Collect-then-finalize natives buffer every input row in their global state
// during in_out_function and do all the work in in_out_function_final. Exactly
// one local state may do that work, and only after every local state that can
// hold rows has finished collecting.
//
// DuckDB facts this relies on (checked against duckdb v2.0.0):
//   1. PipelineExecutor's constructor creates the local state of every
//      operator in the pipeline (GetOperatorState -> our init_local) BEFORE
//      the executor fetches any source data (pipeline_executor.cpp,
//      constructor). So any executor that holds rows has already called
//      RegisterLocal().
//   2. Each executor calls FinalExecute (our in_out_function_final) once it
//      has finished its input (pipeline_executor.cpp, TryFlushCachingOperators).
//
// Rule: count init_local calls (registered) and first final arrivals
// (arrived). The arrival that makes arrived == registered processes; every
// other arrival returns FINISHED at once. There is no spin-wait (no worker
// thread blocks on another task, so no deadlock when tasks outnumber threads)
// and MaxThreads is not reduced, so input intake stays parallel. Executors
// created after the winner claimed find the source exhausted, hold no rows,
// and simply arrive and finish.
//
// Limit: a UNION ALL (or any other multi-pipeline input) below the operator
// gives it several pipelines sharing ONE global state (pipeline.cpp,
// ResetSinkAndOperators creates op_state once), and each pipeline's executors
// reach final when that pipeline ends. The barrier cannot see executors of a
// pipeline that has not started yet. Callers must therefore hand these natives
// a single-pipeline input: the public macros read their source through
// `(SELECT * FROM _ts_passthrough(source) ORDER BY ALL)`. The ORDER BY is a
// pipeline breaker (a sink whose sorted output is scanned by ONE parallel
// pipeline); the order itself does not matter (the natives sort per group).
// A `WITH ... AS MATERIALIZED` CTE was rejected: on DuckDB v2.0.0 a CTE runs
// through the pipeline broadcast exchange, and a final-emitting in-out
// operator fed by it intermittently raised "INTERNAL Error: Pipeline broadcast
// exchange received invalid producer batch index" (for example under
// count(DISTINCT ..), min(..) GROUP BY); it was also ~9x costlier than the sort.
// AssertCollecting() turns any row that still arrives after the claim into an
// error instead of a silently partial result (defence in depth).
// ============================================================================

#include "duckdb/common/exception.hpp"
#include <mutex>

namespace duckdb {

struct TsCollectFinalizeBarrier {
    std::mutex mutex;
    idx_t registered = 0;
    idx_t arrived = 0;
    bool claimed = false;

    // Call from init_local (once per local state).
    void RegisterLocal() {
        std::lock_guard<std::mutex> guard(mutex);
        registered++;
    }

    // Call once per local state, on its first in_out_function_final. Returns
    // true exactly once: for the arrival that makes arrived == registered.
    bool ArriveAtFinal() {
        std::lock_guard<std::mutex> guard(mutex);
        arrived++;
        if (!claimed && arrived == registered) {
            claimed = true;
            return true;
        }
        return false;
    }

    bool Claimed() {
        std::lock_guard<std::mutex> guard(mutex);
        return claimed;
    }

    // Call at the start of in_out_function.
    void AssertCollecting(const char *function_name) {
        if (Claimed()) {
            throw InvalidInputException(
                "%s: input rows arrived after the function had finalized. Its input had more than one "
                "pipeline (for example a UNION ALL); pass it through a pipeline breaker first, e.g. "
                "(SELECT * FROM _ts_passthrough(source) ORDER BY ALL) as the public ts_*_by macros do.",
                function_name);
        }
    }
};

} // namespace duckdb
