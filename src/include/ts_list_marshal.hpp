#pragma once

// The ONLY list-extraction code for scalar functions (Phase 12 D-01). NULLs are
// passed to Rust as validity; the missing-data policy lives in
// crates/anofox-fcst-core/src/missing.rs.
//
// Model: src/table_functions/ts_stats.cpp:99-135 (ExtractListDoubles) — every
// LIST(DOUBLE) argument must be read via ToUnifiedFormat on BOTH the list vector
// and its child vector so constant/dictionary-encoded arguments (e.g. a literal
// `[0,0,0]` broadcast across every row) give correct per-row results, not just
// row 0.

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "anofox_fcst_ffi.h"

namespace duckdb {

// Reads a LIST(DOUBLE) argument for every row of a chunk. Construct once per
// chunk per list argument (NOT once per row) — the constructor does the
// ToUnifiedFormat work.
class ListDoubleReader {
public:
	ListDoubleReader(Vector &list_vec, idx_t count) : list_vec(list_vec) {
		list_vec.ToUnifiedFormat(count, list_data);
		auto &child_vec_ref = ListVector::GetEntry(list_vec);
		child_vec_ref.ToUnifiedFormat(ListVector::GetListSize(list_vec), child_data);
	}

	// True if the top-level list argument itself is NULL for this row.
	// Correct for CONSTANT vectors at row_idx > 0 (unlike FlatVector::IsNull on
	// the list argument, which only sees row 0's validity under VectorType::CONSTANT_VECTOR).
	bool IsNull(idx_t row_idx) const {
		auto list_idx = list_data.sel->get_index(row_idx);
		return !list_data.validity.RowIsValid(list_idx);
	}

	// Fills `values` (size n, 0.0 placeholder at NULL elements) and `validity`
	// ((n+63)/64 words, bit i set iff element i is valid). NULLs are NEVER
	// dropped here — only marshalled through as a validity bitmask. Returns n.
	idx_t Read(idx_t row_idx, vector<double> &values, vector<uint64_t> &validity) const {
		auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(list_data);
		auto list_idx = list_data.sel->get_index(row_idx);
		auto &list_entry = list_entries[list_idx];

		auto child_values = UnifiedVectorFormat::GetData<double>(child_data);

		idx_t n = list_entry.length;
		idx_t list_offset = list_entry.offset;

		values.clear();
		validity.clear();
		values.resize(n);
		size_t validity_words = (n + 63) / 64;
		validity.resize(validity_words, 0);

		for (idx_t i = 0; i < n; i++) {
			idx_t child_idx = list_offset + i;
			auto unified_child_idx = child_data.sel->get_index(child_idx);
			if (child_data.validity.RowIsValid(unified_child_idx)) {
				values[i] = child_values[unified_child_idx];
				validity[i / 64] |= (1ULL << (i % 64));
			} else {
				values[i] = 0.0;
			}
		}
		return n;
	}

	// For PARAMETER lists (quantile levels, alphas, ...), where a NULL element
	// is a user error, not a data-missingness case. Throws InvalidInputException
	// naming the 1-based position of the first NULL; otherwise fills `values`
	// with every element.
	void ReadNoNulls(idx_t row_idx, vector<double> &values, const char *fn_name,
	                  const char *param_name) const {
		auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(list_data);
		auto list_idx = list_data.sel->get_index(row_idx);
		auto &list_entry = list_entries[list_idx];

		auto child_values = UnifiedVectorFormat::GetData<double>(child_data);

		idx_t n = list_entry.length;
		idx_t list_offset = list_entry.offset;

		values.clear();
		values.resize(n);

		for (idx_t i = 0; i < n; i++) {
			idx_t child_idx = list_offset + i;
			auto unified_child_idx = child_data.sel->get_index(child_idx);
			if (!child_data.validity.RowIsValid(unified_child_idx)) {
				throw InvalidInputException("%s: %s must not contain NULL (position %llu)", fn_name,
				                             param_name, (unsigned long long)(i + 1));
			}
			values[i] = child_values[unified_child_idx];
		}
	}

private:
	Vector &list_vec;
	UnifiedVectorFormat list_data;
	UnifiedVectorFormat child_data;
};

// Reads a LIST(LIST(DOUBLE)) argument (e.g. the mqloss quantile matrix) for
// every row of a chunk. Unified at all three levels (outer list, inner list,
// innermost DOUBLE child).
class NestedListDoubleReader {
public:
	NestedListDoubleReader(Vector &list_vec, idx_t count) : list_vec(list_vec) {
		list_vec.ToUnifiedFormat(count, outer_data);
		auto &inner_vec_ref = ListVector::GetEntry(list_vec);
		inner_vec_ref.ToUnifiedFormat(ListVector::GetListSize(list_vec), inner_data);
		auto &innermost_vec_ref = ListVector::GetEntry(inner_vec_ref);
		innermost_vec_ref.ToUnifiedFormat(ListVector::GetListSize(inner_vec_ref), innermost_data);
	}

	bool IsNull(idx_t row_idx) const {
		auto outer_idx = outer_data.sel->get_index(row_idx);
		return !outer_data.validity.RowIsValid(outer_idx);
	}

	// A NULL inner list (one quantile level's forecast array) produces an
	// all-invalid row in `validity[level]` rather than being dropped.
	idx_t Read(idx_t row_idx, vector<vector<double>> &values, vector<vector<uint64_t>> &validity) const {
		auto outer_entries = UnifiedVectorFormat::GetData<list_entry_t>(outer_data);
		auto outer_idx = outer_data.sel->get_index(row_idx);
		auto &outer_entry = outer_entries[outer_idx];

		auto inner_entries = UnifiedVectorFormat::GetData<list_entry_t>(inner_data);
		auto innermost_values = UnifiedVectorFormat::GetData<double>(innermost_data);

		idx_t n_levels = outer_entry.length;
		idx_t outer_offset = outer_entry.offset;

		values.clear();
		validity.clear();
		values.resize(n_levels);
		validity.resize(n_levels);

		for (idx_t level = 0; level < n_levels; level++) {
			idx_t inner_list_idx_raw = outer_offset + level;
			auto inner_unified_idx = inner_data.sel->get_index(inner_list_idx_raw);

			auto &values_out = values[level];
			auto &validity_out = validity[level];

			if (!inner_data.validity.RowIsValid(inner_unified_idx)) {
				// NULL inner list: all-invalid row of length 0 (caller treats as
				// "no data at this level" — downstream policy decides).
				continue;
			}

			auto &inner_entry = inner_entries[inner_unified_idx];
			idx_t m = inner_entry.length;
			idx_t inner_offset = inner_entry.offset;

			values_out.resize(m);
			size_t validity_words = (m + 63) / 64;
			validity_out.resize(validity_words, 0);

			for (idx_t j = 0; j < m; j++) {
				idx_t innermost_idx = inner_offset + j;
				auto unified_innermost_idx = innermost_data.sel->get_index(innermost_idx);
				if (innermost_data.validity.RowIsValid(unified_innermost_idx)) {
					values_out[j] = innermost_values[unified_innermost_idx];
					validity_out[j / 64] |= (1ULL << (j % 64));
				} else {
					values_out[j] = 0.0;
				}
			}
		}
		return n_levels;
	}

private:
	Vector &list_vec;
	UnifiedVectorFormat outer_data;
	UnifiedVectorFormat inner_data;
	UnifiedVectorFormat innermost_data;
};

// Writes a flat double array into row `row_idx` of a LIST(DOUBLE) result vector.
inline void WriteDoubleList(Vector &list_vec, idx_t row_idx, const double *data, idx_t n) {
	idx_t offset = ListVector::GetListSize(list_vec);
	ListVector::Reserve(list_vec, offset + n);
	ListVector::SetListSize(list_vec, offset + n);

	auto &child_vec = ListVector::GetEntry(list_vec);
	auto child_data = FlatVector::GetData<double>(child_vec);
	for (idx_t i = 0; i < n; i++) {
		child_data[offset + i] = data[i];
	}

	auto list_entries = FlatVector::GetData<list_entry_t>(list_vec);
	list_entries[row_idx].offset = offset;
	list_entries[row_idx].length = n;
}

// Same as WriteDoubleList, but NaN is the Rust-side missing marker in returned
// arrays and is written out as a SQL NULL element rather than the literal double.
inline void WriteDoubleListNanAsNull(Vector &list_vec, idx_t row_idx, const double *data, idx_t n) {
	idx_t offset = ListVector::GetListSize(list_vec);
	ListVector::Reserve(list_vec, offset + n);
	ListVector::SetListSize(list_vec, offset + n);

	auto &child_vec = ListVector::GetEntry(list_vec);
	auto child_data = FlatVector::GetData<double>(child_vec);
	auto &child_validity = FlatVector::Validity(child_vec);
	for (idx_t i = 0; i < n; i++) {
		if (std::isnan(data[i])) {
			child_validity.SetInvalid(offset + i);
		} else {
			child_data[offset + i] = data[i];
		}
	}

	auto list_entries = FlatVector::GetData<list_entry_t>(list_vec);
	list_entries[row_idx].offset = offset;
	list_entries[row_idx].length = n;
}

} // namespace duckdb
