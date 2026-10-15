#!/usr/bin/env python3
"""Length-only Rust test sweep (Phase 18, 18-02, D-03, TEST-02).

Scans the Rust modules touched by the v0.10.0 Statistical Correctness milestone
(frozen MILESTONE_MODULES, computed from `git diff --name-only MILESTONE_BASE..HEAD
-- crates` at the time this script was written) for `#[test]` functions whose
assertions only inspect length/shape/presence -- never an actual value -- and
which carry no error-path expectation. Those tests would pass even if the
function under test returned the right SHAPE of garbage data, so corrected
statistical behaviour fixed earlier in this milestone could silently regress
without any test catching it.

Usage:
    python3 scripts/check_length_only_tests.py                 # run the sweep
    python3 scripts/check_length_only_tests.py --check-module-list  # verify the
                                                                      frozen list
                                                                      still matches git
    python3 scripts/check_length_only_tests.py --self-test      # run the script's
                                                                   own synthetic unit
                                                                   tests

Exit codes:
    0 - clean (sweep: no un-allow-listed finding; self-test: all assertions passed;
        check-module-list: frozen list matches git)
    1 - findings printed to stderr (sweep), or the module list has drifted
        (--check-module-list), or a self-test assertion failed

Allow-list format (scripts/length_only_allowlist.txt): pipe-delimited lines
    path | test_fn | justification
`#` starts a comment line. An entry is valid only for a heuristic false positive
(a test that genuinely asserts values through a mechanism this regex-based scan
cannot see, e.g. a list of per-case violations asserted to be empty). An entry
whose justification is empty, or which matches no test the sweep actually
flagged, fails the run (a stale allow-list entry hides real coverage loss).
"""
import os
import re
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = subprocess.run(
    ["git", "rev-parse", "--show-toplevel"], cwd=SCRIPT_DIR, capture_output=True, text=True, check=True
).stdout.strip()

ALLOWLIST_PATH = os.path.join(SCRIPT_DIR, "length_only_allowlist.txt")

MILESTONE_BASE = "b1261fe"

# Frozen at Phase 18 plan 18-02 execution time (`git diff --name-only
# b1261fe..HEAD -- crates`, filtered to *.rs). Frozen so the sweep is
# reproducible on a shallow CI checkout that cannot run `git diff` against a
# commit outside its clone depth. Use --check-module-list (local dev only) to
# confirm this still matches a full checkout.
MILESTONE_MODULES = sorted([
    "crates/anofox-fcst-core/src/bootstrap.rs",
    "crates/anofox-fcst-core/src/calendar.rs",
    "crates/anofox-fcst-core/src/changepoint.rs",
    "crates/anofox-fcst-core/src/conformal.rs",
    "crates/anofox-fcst-core/src/decomposition.rs",
    "crates/anofox-fcst-core/src/detrending.rs",
    "crates/anofox-fcst-core/src/features.rs",
    "crates/anofox-fcst-core/src/filter.rs",
    "crates/anofox-fcst-core/src/forecast.rs",
    "crates/anofox-fcst-core/src/gaps.rs",
    "crates/anofox-fcst-core/src/imputation.rs",
    "crates/anofox-fcst-core/src/lib.rs",
    "crates/anofox-fcst-core/src/metrics.rs",
    "crates/anofox-fcst-core/src/missing.rs",
    "crates/anofox-fcst-core/src/peaks.rs",
    "crates/anofox-fcst-core/src/periods.rs",
    "crates/anofox-fcst-core/src/quality.rs",
    "crates/anofox-fcst-core/src/seasonality.rs",
    "crates/anofox-fcst-core/src/stats.rs",
    "crates/anofox-fcst-core/src/stl.rs",
    "crates/anofox-fcst-core/src/supsmu.rs",
    "crates/anofox-fcst-core/src/validation.rs",
    "crates/anofox-fcst-core/tests/changepoint_reference.rs",
    "crates/anofox-fcst-core/tests/common/mod.rs",
    "crates/anofox-fcst-core/tests/conformal_reference.rs",
    "crates/anofox-fcst-core/tests/dependency_bump_regressions.rs",
    "crates/anofox-fcst-core/tests/detrend_reference.rs",
    "crates/anofox-fcst-core/tests/exog_ols_memory.rs",
    "crates/anofox-fcst-core/tests/forecast_arima.rs",
    "crates/anofox-fcst-core/tests/forecast_exog.rs",
    "crates/anofox-fcst-core/tests/forecast_fallbacks.rs",
    "crates/anofox-fcst-core/tests/forecast_intervals.rs",
    "crates/anofox-fcst-core/tests/forecast_snapshot.rs",
    "crates/anofox-fcst-core/tests/interval_coverage_mc.rs",
    "crates/anofox-fcst-core/tests/metrics_reference.rs",
    "crates/anofox-fcst-core/tests/mstl_decompose_reference.rs",
    "crates/anofox-fcst-core/tests/peaks_reference.rs",
    "crates/anofox-fcst-core/tests/period_detection_reference.rs",
    "crates/anofox-fcst-core/tests/portmanteau_reference.rs",
    "crates/anofox-fcst-core/tests/quality_gaps_reference.rs",
    "crates/anofox-fcst-core/tests/reference_fixtures.rs",
    "crates/anofox-fcst-core/tests/r_forecast_parity.rs",
    "crates/anofox-fcst-core/tests/r_parity_diagnosis.rs",
    "crates/anofox-fcst-core/tests/stationarity_reference.rs",
    "crates/anofox-fcst-core/tests/stats_reference.rs",
    "crates/anofox-fcst-core/tests/stl_reference.rs",
    "crates/anofox-fcst-core/tests/theta_croston_fitted.rs",
    "crates/anofox-fcst-core/tests/tsfresh_features_reference.rs",
    "crates/anofox-fcst-ffi/src/lib.rs",
    "crates/anofox-fcst-ffi/src/types.rs",
    "crates/anofox-fcst-ffi/tests/calendar_ffi.rs",
    "crates/anofox-fcst-ffi/tests/core_ffi_parity.rs",
    "crates/anofox-fcst-ffi/tests/null_policy_ffi.rs",
])

ASSERT_MACRO_RE = re.compile(
    r"\b(assert_eq|assert_ne|debug_assert_eq|debug_assert_ne|assert|debug_assert)!\s*\(|"
    r"\b(assert_close|assert_vec_close)\s*\("
)

ATTR_LINE_RE = re.compile(r"^\s*#\[")
FN_LINE_RE = re.compile(r"^\s*(pub(\([^)]*\))?\s+)?(async\s+)?fn\s+(\w+)\s*(<[^>]*>)?\s*\(")

# "Length-only" value expressions: an assertion's value-bearing side counts as
# length-only when, after stripping whitespace, it fully matches one of these
# (a bare accessor call with no comparison against a non-length value) -- OR
# is a bare integer literal (the "other side" of a `.len() == N` comparison).
LENGTH_EXPR_RE = re.compile(
    r"^[\w:\.\[\]]+\.len\(\)$"
    r"|^[\w:\.\[\]]+\.is_empty\(\)$"
    r"|^[\w:\.\[\]]+\.is_some\(\)$"
    r"|^[\w:\.\[\]]+\.is_ok\(\)$"
    r"|^[\w:\.\[\]]+\.is_finite\(\)$"
    r"|^[\w:\.\[\]]*is_nan\([\w:\.\[\]]*\)$"
    r"|^[\w:\.\[\]]+\.count\(\)$"
    r"|^[\w:\.\[\]]+\.shape\(\)$"
    r"|^[\w:\.\[\]]*\.?(nrows|ncols)(\(\))?$"
)
BARE_INT_RE = re.compile(r"^-?\d+$")

# Error-path expectations anywhere in the body/attrs exempt a test entirely,
# even if every individual assertion would otherwise look length-only.
ERROR_PATH_RE = re.compile(
    r"\.is_err\(|unwrap_err|expect_err|\bErr\(|\.is_none\("
)
SHOULD_PANIC_RE = re.compile(r"#\[\s*should_panic")


def split_top_level_commas(s):
    """Split a Rust expression list on top-level commas (ignoring commas inside
    (), [], {}, or string/char literals)."""
    parts = []
    depth = 0
    cur = []
    i = 0
    in_str = False
    in_char = False
    n = len(s)
    while i < n:
        c = s[i]
        if in_str:
            cur.append(c)
            if c == "\\":
                if i + 1 < n:
                    cur.append(s[i + 1])
                    i += 2
                    continue
            elif c == '"':
                in_str = False
            i += 1
            continue
        if in_char:
            cur.append(c)
            if c == "\\":
                if i + 1 < n:
                    cur.append(s[i + 1])
                    i += 2
                    continue
            elif c == "'":
                in_char = False
            i += 1
            continue
        if c == '"':
            in_str = True
            cur.append(c)
        elif c == "'":
            in_char = True
            cur.append(c)
        elif c in "([{":
            depth += 1
            cur.append(c)
        elif c in ")]}":
            depth -= 1
            cur.append(c)
        elif c == "," and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    parts.append("".join(cur))
    return [p.strip() for p in parts]


def find_matching_paren(text, open_idx):
    """Given the index of an opening '(' in text, find the index of its
    matching ')', skipping over string/char literals and nested parens."""
    depth = 0
    i = open_idx
    n = len(text)
    in_str = False
    in_char = False
    while i < n:
        c = text[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if in_char:
            if c == "\\":
                i += 2
                continue
            if c == "'":
                in_char = False
            i += 1
            continue
        if c == '"':
            in_str = True
        elif c == "'":
            in_char = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def comparison_sides(expr):
    """Split a boolean expression on a top-level comparison operator
    (==, !=, >=, <=, >, <), returning (left, right) or None if there isn't one
    (a bare predicate like `x.is_empty()`)."""
    # Scan left to right outside of parens/strings for a comparison operator.
    depth = 0
    i = 0
    n = len(expr)
    in_str = False
    while i < n:
        c = expr[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif depth == 0 and c in "=!><":
            for op in ("==", "!=", ">=", "<="):
                if expr[i:i + 2] == op:
                    return expr[:i].strip(), expr[i + 2:].strip()
            if c in "><":
                return expr[:i].strip(), expr[i + 1:].strip()
        i += 1
    return None


def is_length_only_value(expr):
    expr = expr.strip()
    if expr.endswith("== true"):
        expr = expr[: -len("== true")].strip()
    nospace = re.sub(r"\s+", "", expr)
    if LENGTH_EXPR_RE.match(nospace):
        return True
    if BARE_INT_RE.match(nospace):
        return True
    return False


def assertion_is_length_only(macro_name, args):
    """Classify a single assertion call's arguments. `args` is the
    comma-split (top-level) argument list; the trailing format-string message
    argument(s), if present beyond what the macro needs, are ignored."""
    if macro_name in ("assert", "debug_assert"):
        cond = args[0] if args else ""
        sides = comparison_sides(cond)
        if sides is None:
            return is_length_only_value(cond)
        left, right = sides
        return is_length_only_value(left) and is_length_only_value(right)
    if macro_name in ("assert_eq", "assert_ne", "debug_assert_eq", "debug_assert_ne"):
        if len(args) < 2:
            return False
        return is_length_only_value(args[0]) and is_length_only_value(args[1])
    if macro_name in ("assert_close", "assert_vec_close"):
        # (got, want, ...) -- these always compare real values, never length-only.
        return False
    return False


def extract_assertions(body):
    """Return a list of booleans, one per assertion call found in `body`,
    True if that call's arguments are length-only."""
    results = []
    for m in ASSERT_MACRO_RE.finditer(body):
        macro_name = m.group(1) or m.group(2)
        open_paren = body.index("(", m.end() - 1)
        close_paren = find_matching_paren(body, open_paren)
        if close_paren == -1:
            continue
        arg_text = body[open_paren + 1:close_paren]
        args = split_top_level_commas(arg_text)
        results.append(assertion_is_length_only(macro_name, args))
    return results


def find_matching_brace(text, open_idx):
    depth = 0
    i = open_idx
    n = len(text)
    in_str = False
    in_char = False
    in_line_comment = False
    in_block_comment = False
    while i < n:
        c = text[i]
        if in_line_comment:
            if c == "\n":
                in_line_comment = False
            i += 1
            continue
        if in_block_comment:
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                in_block_comment = False
                i += 2
                continue
            i += 1
            continue
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if in_char:
            if c == "\\":
                i += 2
                continue
            if c == "'":
                in_char = False
            i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            in_line_comment = True
            i += 2
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            in_block_comment = True
            i += 2
            continue
        if c == '"':
            in_str = True
        elif c == "'":
            in_char = True
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def find_test_functions(text):
    """Returns a list of dicts: {name, body, has_should_panic}."""
    lines = text.splitlines(keepends=True)
    offsets = []
    pos = 0
    for line in lines:
        offsets.append(pos)
        pos += len(line)

    tests = []
    pending_attrs = []
    for idx, line in enumerate(lines):
        stripped = line.strip()
        if not stripped:
            continue
        if ATTR_LINE_RE.match(line):
            pending_attrs.append(line)
            continue
        m = FN_LINE_RE.match(line)
        if m and any(re.match(r"^\s*#\[\s*test\s*(\(.*\))?\s*\]\s*$", a) for a in pending_attrs):
            name = m.group(4)
            has_should_panic = any(SHOULD_PANIC_RE.search(a) for a in pending_attrs)
            # Find the function body: scan forward from this line for the
            # first unmatched '{' at depth 0 relative to the signature, then
            # match to its closing brace.
            sig_start = offsets[idx]
            brace_search_start = sig_start
            open_brace = text.find("{", brace_search_start)
            if open_brace != -1:
                close_brace = find_matching_brace(text, open_brace)
                if close_brace != -1:
                    body = text[open_brace:close_brace + 1]
                    tests.append({"name": name, "body": body, "has_should_panic": has_should_panic})
        pending_attrs = []
    return tests


def sweep_file(path):
    """Returns (findings, zero_assertion_names) for one file."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    findings = []
    zero_assertion = []
    for test in find_test_functions(text):
        body = test["body"]
        if test["has_should_panic"]:
            continue
        if ERROR_PATH_RE.search(body):
            continue
        classifications = extract_assertions(body)
        if not classifications:
            zero_assertion.append(test["name"])
            continue
        if all(classifications):
            findings.append(test["name"])
    return findings, zero_assertion


def read_allowlist():
    """Returns dict[(path, test_fn)] -> justification."""
    entries = {}
    if not os.path.isfile(ALLOWLIST_PATH):
        return entries
    with open(ALLOWLIST_PATH, encoding="utf-8") as f:
        for line in f:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            parts = [p.strip() for p in stripped.split("|")]
            if len(parts) != 3:
                print(f"ERROR: malformed allow-list line: {line!r}", file=sys.stderr)
                sys.exit(1)
            path, test_fn, justification = parts
            if not justification:
                print(f"ERROR: allow-list entry for {path}::{test_fn} has an empty justification", file=sys.stderr)
                sys.exit(1)
            entries[(path, test_fn)] = justification
    return entries


def run_sweep():
    allowlist = read_allowlist()
    used_allowlist_keys = set()
    total_findings = 0
    for rel_path in MILESTONE_MODULES:
        abs_path = os.path.join(REPO_ROOT, rel_path)
        if not os.path.isfile(abs_path):
            print(f"INFO: {rel_path} no longer exists, skipping", file=sys.stderr)
            continue
        findings, zero_assertion = sweep_file(abs_path)
        for name in zero_assertion:
            print(f"INFO (zero-assertion, not a failure): {rel_path}::{name}", file=sys.stderr)
        for name in findings:
            key = (rel_path, name)
            if key in allowlist:
                used_allowlist_keys.add(key)
                continue
            print(f"{rel_path}::{name}", file=sys.stderr)
            total_findings += 1

    stale = set(allowlist.keys()) - used_allowlist_keys
    for path, name in sorted(stale):
        print(f"ERROR: stale allow-list entry (not flagged by the sweep): {path}::{name}", file=sys.stderr)

    if stale:
        return 1
    if total_findings > 0:
        print(f"\nSummary: {total_findings} length-only test(s) found.", file=sys.stderr)
        return 1
    print("Summary: 0 un-allow-listed length-only tests.", file=sys.stderr)
    return 0


def check_module_list():
    result = subprocess.run(
        ["git", "diff", "--name-only", f"{MILESTONE_BASE}..HEAD", "--", "crates"],
        cwd=REPO_ROOT, capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"ERROR: git diff failed: {result.stderr}", file=sys.stderr)
        return 2
    live = sorted(p for p in result.stdout.splitlines() if p.endswith(".rs"))
    frozen = sorted(MILESTONE_MODULES)
    if live != frozen:
        only_live = sorted(set(live) - set(frozen))
        only_frozen = sorted(set(frozen) - set(live))
        if only_live:
            print("Modules touched by git but not in the frozen MILESTONE_MODULES list:", file=sys.stderr)
            for p in only_live:
                print(f"  + {p}", file=sys.stderr)
        if only_frozen:
            print("Modules in the frozen MILESTONE_MODULES list but no longer touched by git:", file=sys.stderr)
            for p in only_frozen:
                print(f"  - {p}", file=sys.stderr)
        return 1
    print("MILESTONE_MODULES matches `git diff --name-only` exactly.", file=sys.stderr)
    return 0


def self_test():
    failures = 0

    def check(desc, condition):
        nonlocal failures
        status = "PASS" if condition else "FAIL"
        print(f"[{status}] {desc}", file=sys.stderr)
        if not condition:
            failures += 1

    # A test asserting only `out.len() == 5` is flagged.
    src1 = """
#[test]
fn length_only() {
    let out = vec![1, 2, 3, 4, 5];
    assert_eq!(out.len(), 5);
}
"""
    findings1, zero1 = sweep_text_for_test(src1, "length_only")
    check("out.len() == 5 is flagged", "length_only" in findings1)

    # A test asserting `out[0] == 1.5` is not flagged (real value comparison).
    src2 = """
#[test]
fn value_check() {
    let out = vec![1.5, 2.5];
    assert_eq!(out[0], 1.5);
}
"""
    findings2, _ = sweep_text_for_test(src2, "value_check")
    check("out[0] == 1.5 is NOT flagged", "value_check" not in findings2)

    # A test asserting `result.is_err()` is not flagged (error-path expectation).
    src3 = """
#[test]
fn error_path() {
    let result: Result<(), String> = Err("boom".to_string());
    assert!(result.is_err());
}
"""
    findings3, _ = sweep_text_for_test(src3, "error_path")
    check("result.is_err() is NOT flagged (error-path)", "error_path" not in findings3)

    # A #[should_panic] test is not flagged, even with a length-only assertion.
    src4 = """
#[test]
#[should_panic]
fn panics() {
    let out: Vec<i32> = vec![];
    assert!(out.is_empty());
}
"""
    findings4, _ = sweep_text_for_test(src4, "panics")
    check("#[should_panic] test is NOT flagged", "panics" not in findings4)

    # A test asserting `violations.is_empty()` IS flagged (heuristic limit;
    # needs an allow-list entry in real use).
    src5 = """
#[test]
fn violations_empty() {
    let violations: Vec<String> = vec![];
    assert!(violations.is_empty());
}
"""
    findings5, _ = sweep_text_for_test(src5, "violations_empty")
    check("violations.is_empty() IS flagged (heuristic limit)", "violations_empty" in findings5)

    # An allow-list entry naming a test that is not flagged makes the run fail.
    import tempfile
    with tempfile.TemporaryDirectory() as tmpdir:
        rust_path = os.path.join(tmpdir, "stale_mod.rs")
        with open(rust_path, "w", encoding="utf-8") as f:
            f.write(src2)  # value_check -- never flagged
        allowlist_path = os.path.join(tmpdir, "allowlist.txt")
        with open(allowlist_path, "w", encoding="utf-8") as f:
            f.write("stale_mod.rs | value_check | fake justification for a test that is never flagged\n")
        findings, zero = sweep_file(rust_path)
        allow_entries = {}
        with open(allowlist_path, encoding="utf-8") as f:
            for line in f:
                parts = [p.strip() for p in line.strip().split("|")]
                if len(parts) == 3:
                    allow_entries[(parts[0], parts[1])] = parts[2]
        stale = set(allow_entries.keys()) - {("stale_mod.rs", n) for n in findings}
        check("a stale allow-list entry is detected", len(stale) == 1)

    return 1 if failures > 0 else 0


def sweep_text_for_test(text, expected_name):
    """Helper for --self-test: run the sweep logic against an in-memory
    source string instead of a file on disk."""
    import tempfile
    with tempfile.NamedTemporaryFile(mode="w", suffix=".rs", delete=False, encoding="utf-8") as f:
        f.write(text)
        path = f.name
    try:
        return sweep_file(path)
    finally:
        os.unlink(path)


def main():
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    if "--check-module-list" in sys.argv:
        sys.exit(check_module_list())
    sys.exit(run_sweep())


if __name__ == "__main__":
    main()
