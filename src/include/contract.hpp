#pragma once

#include "duckdb.hpp"

namespace duckdb {

class ExtensionLoader;

// anofox integration contract (anofox-visualization docs/plans/integration-contract.md,
// #279). Every anofox extension returns the same version from anofox_contract_version().
constexpr const char *ANOFOX_CONTRACT_VERSION = "1";

// Names of the functions and macros in the system catalog's main schema.
// LoadInternal takes this before registering anything, so the tagging pass can
// tell this extension's entries from those of DuckDB or other extensions.
case_insensitive_set_t SnapshotSystemFunctionNames(ExtensionLoader &loader);

// Registers anofox_contract_version() unless another anofox extension already
// did (`preexisting` holds it); a second identical overload would fail the load.
void RegisterContractVersionFunction(ExtensionLoader &loader, const case_insensitive_set_t &preexisting);

// Tags every function this extension registered (except the shared
// anofox_contract_version()) with anofox.family = 'forecast', and each contract
// producer with anofox.contract and anofox.output (its schema).
void ApplyContractTags(ExtensionLoader &loader, const case_insensitive_set_t &preexisting);

} // namespace duckdb
