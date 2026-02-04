# Parallel Execution for DuckDB Table In-Out Functions

## Problem

DuckDB's `PhysicalTableInOutFunction::ParallelOperator()` is **hardcoded to return `true`**, ignoring the `MaxThreads()` hint from table functions. This means:

1. Multiple threads **always** call `in_out_function` concurrently
2. Multiple threads **always** call `in_out_function_final` concurrently
3. The `MaxThreads()` return value is ignored for scheduling

This causes issues for stateful table functions that accumulate data across batches:
- **Race conditions** when multiple threads write to shared state
- **Duplicate output** when multiple threads read/output from shared state
- **Data loss** when threads overwrite each other's work

## Failed Approaches

### 1. Single Global Mutex (Serialization)
```cpp
struct GlobalState {
    mutex mtx;
    map<string, GroupData> groups;
};

// In InOut:
lock_guard<mutex> lock(gstate.mtx);
// ... process all rows under lock
```

**Result**: Correct but defeats parallelism entirely. All threads serialize on the mutex.

### 2. Hash-Based Thread Ownership (Filtering)
```cpp
// Each thread only processes groups where hash(group) % num_threads == my_thread_id
if (hash(group_key) % num_threads != my_thread_id) continue;
```

**Result**: Incorrect. DuckDB partitions input data across threads, so each thread only sees a subset of rows. Filtering by hash causes data loss since rows for a group may arrive at a thread that doesn't "own" that group.

### 3. Per-Slot Mutex with Per-Row Locking
```cpp
// Lock the slot for each row
for (each row) {
    idx_t slot = hash(group_key) % num_slots;
    lock_guard<mutex> lock(slots[slot].mtx);
    // insert row
}
```

**Result**: Correct but slow. Lock acquisition per row creates significant overhead, often worse than single global mutex.

## Working Solution: Hash-Based Slot Partitioning with Batched Locking

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Global State                           │
├─────────────────────────────────────────────────────────────┤
│  Slot 0          Slot 1          Slot 2         Slot N-1   │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐  │
│  │ mutex   │    │ mutex   │    │ mutex   │    │ mutex   │  │
│  │ groups  │    │ groups  │    │ groups  │    │ groups  │  │
│  │ results │    │ results │    │ results │    │ results │  │
│  └─────────┘    └─────────┘    └─────────┘    └─────────┘  │
│       ↑              ↑              ↑              ↑       │
│       │              │              │              │       │
│  hash(g)%N==0   hash(g)%N==1   hash(g)%N==2   hash(g)%N==N-1│
└─────────────────────────────────────────────────────────────┘
```

### Key Insight

Instead of locking per-row, **batch rows locally first**, then lock each slot **once per batch**:

```cpp
// Step 1: Collect rows locally by slot (NO LOCKING)
vector<vector<pair<string, Row>>> slot_batches(num_slots);
for (each row in input) {
    idx_t slot = hash(group_key) % num_slots;
    slot_batches[slot].push_back({group_key, row});
}

// Step 2: Lock each slot ONCE and insert all its rows
for (idx_t slot = 0; slot < num_slots; slot++) {
    if (slot_batches[slot].empty()) continue;

    lock_guard<mutex> lock(slots[slot].mtx);
    for (auto& [key, row] : slot_batches[slot]) {
        // insert into slot's groups
    }
}
```

### Benefits

1. **Reduced lock contention**: Lock acquisitions reduced from O(rows) to O(slots_with_data)
2. **Parallel writes**: Different threads can write to different slots concurrently
3. **Correct**: All data is captured regardless of which thread receives it
4. **Scalable**: More slots = less contention (num_slots = num_threads)

### Implementation Structure

```cpp
// Per-slot storage
struct Slot {
    mutex mtx;
    map<string, GroupData> groups;
    vector<string> group_order;

    // Output state
    vector<FilledGroup> results;
    bool processed = false;
    idx_t current_group = 0;
    idx_t current_row = 0;
};

// Global state
struct GlobalState : public GlobalTableFunctionState {
    idx_t num_slots;
    vector<unique_ptr<Slot>> slots;

    idx_t MaxThreads() const override { return num_slots; }
};

// Local state (for output iteration)
struct LocalState : public LocalTableFunctionState {
    idx_t current_slot = 0;
};
```

### InOut Function (Input Phase)

```cpp
static OperatorResultType InOut(...) {
    // Step 1: Batch rows locally by slot
    vector<vector<pair<string, TempRow>>> slot_batches(num_slots);

    for (idx_t i = 0; i < input.size(); i++) {
        string group_key = GetGroupKey(input.data[0].GetValue(i));
        idx_t slot_idx = Hash(group_key) % num_slots;

        TempRow row = { /* extract values */ };
        slot_batches[slot_idx].push_back({group_key, row});
    }

    // Step 2: Lock each slot once and insert batch
    for (idx_t slot_idx = 0; slot_idx < num_slots; slot_idx++) {
        if (slot_batches[slot_idx].empty()) continue;

        lock_guard<mutex> lock(slots[slot_idx]->mtx);
        for (auto& [key, row] : slot_batches[slot_idx]) {
            // Insert into slot's groups map
        }
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}
```

### Finalize Function (Output Phase)

```cpp
static OperatorFinalizeResultType Finalize(...) {
    // Each thread iterates through slots sequentially
    while (lstate.current_slot < num_slots) {
        auto& slot = *gstate.slots[lstate.current_slot];

        lock_guard<mutex> lock(slot.mtx);

        // Process slot if not yet done
        if (!slot.processed) {
            for (auto& group : slot.groups) {
                // Call FFI, store results
            }
            slot.processed = true;
        }

        // Output from this slot
        if (slot has more output) {
            // Fill output chunk
            return HAVE_MORE_OUTPUT;
        }

        lstate.current_slot++;
    }

    return FINISHED;
}
```

## Performance Characteristics

| Approach | Lock Acquisitions | Parallelism | Correctness |
|----------|------------------|-------------|-------------|
| Single mutex | O(batches) | None | ✓ |
| Per-row slot locking | O(rows) | Partial | ✓ |
| **Batched slot locking** | O(slots × batches) | Good | ✓ |

The batched approach typically acquires locks `num_slots` times per batch (once per non-empty slot), compared to once per row. With `STANDARD_VECTOR_SIZE = 2048` rows per batch and 8 slots, this is ~256x fewer lock acquisitions.

## Caveats

1. **Memory overhead**: Temporary batch storage adds memory usage proportional to batch size
2. **Hash distribution**: Performance depends on groups being well-distributed across slots
3. **Finalize ordering**: Output order depends on slot iteration order, not input order

## Files

- `src/table_functions/ts_fill_gaps_native.cpp` - Gap filling implementation
- `src/table_functions/ts_fill_forward_native.cpp` - Fill forward implementation
