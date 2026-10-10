# Deferred Items

Out-of-scope discoveries logged during plan execution (per executor deviation rules'
scope boundary). Not fixed here.

## Plan 13-02

- **Pre-existing `cargo clippy --all-targets -- -D warnings` failure, unrelated to this
  plan's files.** `crates/anofox-fcst-core/src/decomposition.rs:70` and
  `crates/anofox-fcst-core/src/features.rs:53` both carry
  `#[allow(clippy::manual_is_multiple_of)]`, which the locally installed clippy
  (both the `stable` default, 0.1.99, and the pinned `1.89.0` toolchain, 0.1.89) rejects
  as `unknown lint` (`-D unknown-lints` escalates it to a hard error). Separately,
  `crates/anofox-fcst-core/src/periods.rs:851` and `:874` trip
  `clippy::needless_range_loop`. `git diff HEAD -- <those three files>` confirms none of
  them were touched by this plan. This blocks a clean whole-workspace
  `cargo clippy --all-targets --all-features -- -D warnings` run locally (Task 3's DoD
  step) regardless of toolchain pin; `stl.rs`/`supsmu.rs`/`stats.rs` (the files this plan
  touches) are themselves clippy-clean — verified by running the same command and
  confirming zero findings reference those three files.
