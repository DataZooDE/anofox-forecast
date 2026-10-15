#!/usr/bin/env python3
"""No-statistics-in-C++/SQL audit (Phase 18, 18-03, D-04, RUST-10).

Scans every `src/**/*.cpp` and `src/**/*.hpp` file on disk for statistical
arithmetic that should live in the Rust crates instead: math-function calls on
data (sqrt/pow/log*/exp*/cbrt/erf*/tgamma/lgamma), std::accumulate-family
reductions, hand-rolled moment loops (a sum/mean/var-ish accumulator or a
mean-from-total-over-count division), and order-statistic sorts
(std::sort/std::stable_sort/std::nth_element/std::partial_sort[_copy]). It also
scans the SQL embedded in C++ raw-string literals (the `ts_macros.cpp` macro
bodies, and any other raw-string SQL) for statistical aggregates (avg, mean,
stddev*, var_pop/var_samp/variance, quantile*/percentile_*/median/mode/mad,
regr_*, corr, covar_*, kurtosis, skewness, entropy, statistical list_*
aggregates) and math functions (sqrt/pow/power/ln/log/exp).

Every hit must match a line-specific entry in scripts/audit_allowlist.txt (same
file, regex searched against the offending stripped line) or the script exits 1
naming it. An allow-list entry with an empty justification, a path that does not
exist, a malformed (not-3-field) line, or an entry that matches no hit (stale)
also fails the run.

Usage:
    python3 scripts/audit_no_stats_in_cpp.py              # run the audit
    python3 scripts/audit_no_stats_in_cpp.py --self-test   # run the script's own
                                                            synthetic unit tests

Exit codes:
    0 - clean (audit: zero violations and zero stale entries; self-test: all
        synthetic cases passed)
    1 - findings printed to stderr (audit), or a self-test assertion failed

Allow-list format (scripts/audit_allowlist.txt): pipe-delimited lines
    path | regex | justification
`#` starts a comment line; blank lines are ignored. `path` is relative to the
repo root. `regex` is a Python regex searched against the stripped text of the
offending line (comments/string contents blanked, but the code itself intact).
`justification` must name which marshalling class applies (row/index ordering by
date or group key; date/timestamp arithmetic; list marshalling (sizes, offsets,
validity); reporting aggregates of Rust-computed per-group results across
groups; counting/structural rollup of already-computed quantities) -- a hit that
actually computes a statistic of series values (mean, variance, quantile, a
scale, a transform) must never be allow-listed.
"""
import os
import re
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = subprocess.run(
    ["git", "rev-parse", "--show-toplevel"], cwd=SCRIPT_DIR, capture_output=True, text=True, check=True
).stdout.strip()

ALLOWLIST_FILE = os.path.join(REPO_ROOT, "scripts", "audit_allowlist.txt")

# Generated header produced by cbindgen from the Rust FFI crate -- not
# hand-written, and not in scope for a "statistics crept into C++" audit.
EXCLUDED_FILES = {"src/include/anofox_fcst_ffi.h"}

# ---------------------------------------------------------------------------
# C++ categories (run against comment/string-stripped code)
# ---------------------------------------------------------------------------
CPP_MATH_RE = re.compile(
    r"\b(?:std::)?(?:sqrt|pow|log1p|log10|log2|log|expm1|exp|cbrt|erfc|erf|tgamma|lgamma)\s*\("
)
CPP_ACCUMULATE_RE = re.compile(r"\bstd::(?:accumulate|reduce|inner_product|transform_reduce)\s*\(")
CPP_ORDER_STAT_RE = re.compile(r"\bstd::(?:stable_sort|partial_sort_copy|partial_sort|nth_element|sort)\s*\(")
# A compound increment into a sum/mean/var/sq/total/acc-named accumulator ...
_MOMENT_IDENT = r"\w*(?:sum|mean|var|sq|total|acc)\w*"
CPP_MOMENT_INCR_RE = re.compile(r"\b" + _MOMENT_IDENT + r"\s*(?:\+=|-=)", re.IGNORECASE)
# ... or dividing such an accumulator by a count-like denominator (mean = total / n).
CPP_MOMENT_DIV_RE = re.compile(
    r"\b" + _MOMENT_IDENT + r"\s*=\s*[^;]*/\s*(?:static_cast<[^>]*>\s*\(\s*)?\w*(?:n|count|size|len)\w*",
    re.IGNORECASE,
)

CPP_CATEGORIES = [
    ("cpp-math", CPP_MATH_RE),
    ("cpp-accumulate", CPP_ACCUMULATE_RE),
    ("cpp-moment-loop", CPP_MOMENT_INCR_RE),
    ("cpp-moment-loop", CPP_MOMENT_DIV_RE),
    ("cpp-order-stat", CPP_ORDER_STAT_RE),
]

# ---------------------------------------------------------------------------
# SQL categories (run against raw-string literal content only, case-insensitive)
# ---------------------------------------------------------------------------
SQL_AGGREGATE_RE = re.compile(
    r"\b(?:avg|mean|stddev\w*|var_pop|var_samp|variance|quantile\w*|percentile_\w*|median|mode|mad"
    r"|regr_\w+|corr|covar_\w+|kurtosis|skewness|entropy"
    r"|list_(?:avg|mean|stddev\w*|var\w*|median|mode|quantile\w*|percentile\w*|entropy|kurtosis|skewness))\s*\(",
    re.IGNORECASE,
)
SQL_MATH_RE = re.compile(r"\b(?:sqrt|power|pow|ln|log|exp)\s*\(", re.IGNORECASE)

SQL_CATEGORIES = [
    ("sql-aggregate", SQL_AGGREGATE_RE),
    ("sql-math", SQL_MATH_RE),
]


class Hit:
    __slots__ = ("path", "line", "category", "text")

    def __init__(self, path, line, category, text):
        self.path = path
        self.line = line
        self.category = category
        self.text = text


def preprocess(text):
    """Strip // and /* */ comments and ordinary "..." string contents from `text`
    (replacing stripped characters with spaces, preserving line numbers and
    newlines exactly), and separately extract every raw-string literal's content
    with the 1-based line number it starts on.

    Returns (stripped_text, raw_segments) where raw_segments is a list of
    (start_line, content) tuples. `content` keeps its own internal newlines so
    per-line SQL matches can be mapped back to an absolute line number.
    """
    n = len(text)
    i = 0
    line = 1
    out = []
    raw_segments = []

    def emit_blanked(span):
        nonlocal line
        for ch in span:
            if ch == "\n":
                out.append("\n")
                line += 1
            else:
                out.append(" ")

    while i < n:
        c = text[i]

        # Raw string literal: R"delim( ... )delim"
        if c == "R" and i + 1 < n and text[i + 1] == '"':
            j = i + 2
            delim_start = j
            while j < n and text[j] not in "( \t\r\n\\)":
                j += 1
            if j < n and text[j] == "(":
                delim = text[delim_start:j]
                end_marker = ")" + delim + '"'
                content_start = j + 1
                end_idx = text.find(end_marker, content_start)
                if end_idx == -1:
                    end_idx = n
                content = text[content_start:end_idx]
                raw_segments.append((line, content))
                full_literal_end = min(end_idx + len(end_marker), n)
                emit_blanked(text[i:full_literal_end])
                i = full_literal_end
                continue

        # Line comment
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = i
            while j < n and text[j] != "\n":
                j += 1
            emit_blanked(text[i:j])
            i = j
            continue

        # Block comment
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = i + 2
            while j < n and not (text[j] == "*" and j + 1 < n and text[j + 1] == "/"):
                j += 1
            j = min(j + 2, n)
            emit_blanked(text[i:j])
            i = j
            continue

        # Ordinary string literal (handles backslash escapes; does not span a
        # raw string, which is caught above before we get here)
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    j += 2
                    continue
                j += 1
            j = min(j + 1, n)
            emit_blanked(text[i:j])
            i = j
            continue

        if c == "\n":
            out.append("\n")
            line += 1
            i += 1
            continue

        out.append(c)
        i += 1

    return "".join(out), raw_segments


def strip_sql_line_comments(content):
    """Strip `-- ...` to end-of-line from each line of raw-string SQL content
    (comment text mentioning an aggregate name, e.g. a doc comment explaining
    AVG()'s NULL semantics, is not a real use of that aggregate)."""
    out_lines = []
    for sql_line in content.split("\n"):
        idx = sql_line.find("--")
        out_lines.append(sql_line[:idx] if idx != -1 else sql_line)
    return "\n".join(out_lines)


def scan_text(path, text):
    """Scan one file's raw text (before any stripping) and return a list of Hit."""
    hits = []
    stripped_text, raw_segments = preprocess(text)

    for line_no, line_text in enumerate(stripped_text.split("\n"), start=1):
        for category, pattern in CPP_CATEGORIES:
            m = pattern.search(line_text)
            if m:
                hits.append(Hit(path, line_no, category, line_text.strip()))

    for start_line, content in raw_segments:
        clean_content = strip_sql_line_comments(content)
        for offset, sql_line in enumerate(clean_content.split("\n")):
            abs_line = start_line + offset
            for category, pattern in SQL_CATEGORIES:
                m = pattern.search(sql_line)
                if m:
                    hits.append(Hit(path, abs_line, category, sql_line.strip()))

    return hits


# ---------------------------------------------------------------------------
# Allow-list
# ---------------------------------------------------------------------------
class AllowEntry:
    __slots__ = ("path", "pattern_text", "pattern", "justification", "lineno", "used", "error")

    def __init__(self, path, pattern_text, justification, lineno):
        self.path = path
        self.pattern_text = pattern_text
        self.justification = justification
        self.lineno = lineno
        self.used = False
        self.error = None
        try:
            self.pattern = re.compile(pattern_text)
        except re.error as e:
            self.pattern = None
            self.error = f"invalid regex: {e}"


def parse_allowlist(allowlist_text, repo_root=None):
    """Parse allow-list text into (entries, malformed) where `malformed` holds
    human-readable diagnostic strings for entries that fail structural
    validation (wrong field count, empty justification, non-existent path, or
    an uncompilable regex)."""
    entries = []
    malformed = []
    for lineno, raw_line in enumerate(allowlist_text.split("\n"), start=1):
        line = raw_line.rstrip("\n")
        if not line.strip() or line.strip().startswith("#"):
            continue
        parts = line.split("|")
        if len(parts) != 3:
            malformed.append(f"audit_allowlist.txt:{lineno}: expected 3 '|'-separated fields, got {len(parts)}")
            continue
        path, pattern_text, justification = (p.strip() for p in parts)
        if not justification:
            malformed.append(f"audit_allowlist.txt:{lineno}: [{path}] empty justification")
        if repo_root is not None and path and not os.path.isfile(os.path.join(repo_root, path)):
            malformed.append(f"audit_allowlist.txt:{lineno}: [{path}] path does not exist")
        entry = AllowEntry(path, pattern_text, justification, lineno)
        if entry.error:
            malformed.append(f"audit_allowlist.txt:{lineno}: [{path}] {entry.error}")
        entries.append(entry)
    return entries, malformed


def run_audit(file_texts, allowlist_text, repo_root=None):
    """Core audit logic, independent of disk I/O so --self-test can exercise it
    with synthetic in-memory inputs.

    Returns dict(hits, allowed, violations, stale, malformed) for the caller to
    report and decide the exit code from.
    """
    entries, malformed = parse_allowlist(allowlist_text, repo_root=repo_root)
    valid_entries = [e for e in entries if e.pattern is not None]

    all_hits = []
    for path, text in file_texts.items():
        all_hits.extend(scan_text(path, text))

    allowed = []
    violations = []
    for hit in all_hits:
        matched = False
        for entry in valid_entries:
            if entry.path != hit.path:
                continue
            if entry.pattern.search(hit.text):
                entry.used = True
                matched = True
        if matched:
            allowed.append(hit)
        else:
            violations.append(hit)

    stale = [e for e in valid_entries if not e.used]

    return {
        "hits": all_hits,
        "allowed": allowed,
        "violations": violations,
        "stale": stale,
        "malformed": malformed,
    }


def gather_files(repo_root):
    src_dir = Path(repo_root) / "src"
    files = sorted(set(src_dir.rglob("*.cpp")) | set(src_dir.rglob("*.hpp")))
    file_texts = {}
    for f in files:
        rel = f.relative_to(repo_root).as_posix()
        if rel in EXCLUDED_FILES:
            continue
        file_texts[rel] = f.read_text(encoding="utf-8", errors="replace")
    return file_texts


def run_real_audit():
    file_texts = gather_files(REPO_ROOT)
    allowlist_text = ""
    if os.path.isfile(ALLOWLIST_FILE):
        with open(ALLOWLIST_FILE, encoding="utf-8") as f:
            allowlist_text = f.read()

    result = run_audit(file_texts, allowlist_text, repo_root=REPO_ROOT)

    for v in result["violations"]:
        print(f"{v.path}:{v.line}: [{v.category}] {v.text}", file=sys.stderr)
    for m in result["malformed"]:
        print(f"[allowlist-error] {m}", file=sys.stderr)
    for e in result["stale"]:
        print(f"[stale] audit_allowlist.txt:{e.lineno}: [{e.path}] {e.pattern_text} -- matched no hit", file=sys.stderr)

    n_stale_and_malformed = len(result["stale"]) + len(result["malformed"])
    print(
        f"audit: {len(result['hits'])} hits, {len(result['allowed'])} allow-listed, "
        f"{len(result['violations'])} violations, {n_stale_and_malformed} stale entries"
    )

    return 0 if not result["violations"] and n_stale_and_malformed == 0 else 1


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------
def self_test():
    failures = []

    def check(name, condition, detail=""):
        if not condition:
            failures.append(f"{name}: {detail}")

    # --- cpp-math positives ---
    text = "double sd = std::sqrt(var);\n"
    hits = scan_text("f.cpp", text)
    check("cpp-math positive (std::sqrt)", any(h.category == "cpp-math" for h in hits), repr(hits))

    text = "double sd = sqrt(var);\n"
    hits = scan_text("f.cpp", text)
    check("cpp-math positive (bare sqrt)", any(h.category == "cpp-math" for h in hits), repr(hits))

    # --- cpp-accumulate positive ---
    text = "auto s = std::accumulate(v.begin(), v.end(), 0.0);\n"
    hits = scan_text("f.cpp", text)
    check("cpp-accumulate positive", any(h.category == "cpp-accumulate" for h in hits), repr(hits))

    # --- cpp-moment-loop positives ---
    text = "sum += values[i];\n"
    hits = scan_text("f.cpp", text)
    check("cpp-moment-loop positive (+=)", any(h.category == "cpp-moment-loop" for h in hits), repr(hits))

    text = "mean = total / static_cast<double>(n);\n"
    hits = scan_text("f.cpp", text)
    check("cpp-moment-loop positive (division)", any(h.category == "cpp-moment-loop" for h in hits), repr(hits))

    # --- cpp-order-stat positive ---
    text = "std::nth_element(v.begin(), v.begin() + k, v.end());\n"
    hits = scan_text("f.cpp", text)
    check("cpp-order-stat positive (nth_element)", any(h.category == "cpp-order-stat" for h in hits), repr(hits))

    text = "std::sort(indices.begin(), indices.end());\n"
    hits = scan_text("f.cpp", text)
    check("cpp-order-stat positive (sort)", any(h.category == "cpp-order-stat" for h in hits), repr(hits))

    # --- negatives: same calls inside comments or ordinary strings are not flagged ---
    text = "// double sd = std::sqrt(var);\n"
    hits = scan_text("f.cpp", text)
    check("negative: line comment", len(hits) == 0, repr(hits))

    text = "/* double sd = std::sqrt(var); std::accumulate(a,b,c); */\n"
    hits = scan_text("f.cpp", text)
    check("negative: block comment", len(hits) == 0, repr(hits))

    text = 'const char *msg = "call std::sqrt(var) to get the stddev";\n'
    hits = scan_text("f.cpp", text)
    check("negative: ordinary string literal", len(hits) == 0, repr(hits))

    # --- SQL positives (raw-string content) ---
    text = 'static const char *q = R"(SELECT stddev(x) FROM t)";\n'
    hits = scan_text("f.cpp", text)
    check("sql-aggregate positive (stddev)", any(h.category == "sql-aggregate" for h in hits), repr(hits))

    text = 'static const char *q = R"(SELECT quantile_cont(x, 0.5) FROM t)";\n'
    hits = scan_text("f.cpp", text)
    check("sql-aggregate positive (quantile_cont)", any(h.category == "sql-aggregate" for h in hits), repr(hits))

    text = 'static const char *q = R"(SELECT list_avg(l) FROM t)";\n'
    hits = scan_text("f.cpp", text)
    check("sql-aggregate positive (list_avg)", any(h.category == "sql-aggregate" for h in hits), repr(hits))

    text = 'static const char *q = R"(SELECT SQRT(v) FROM t)";\n'
    hits = scan_text("f.cpp", text)
    check("sql-math positive (SQRT)", any(h.category == "sql-math" for h in hits), repr(hits))

    # --- SQL negative: a description string (ordinary "...") is never scanned as SQL ---
    text = 'const char *desc = "Computes quantile (pinball) loss per group.";\n'
    hits = scan_text("f.cpp", text)
    check("negative: description string not scanned as SQL", len(hits) == 0, repr(hits))

    # --- allow-list behavior ---
    allowlist = "f.cpp | std::sqrt\\(var\\) | date arithmetic (synthetic test entry)\n"
    file_texts = {"f.cpp": "double sd = std::sqrt(var);\n"}
    result = run_audit(file_texts, allowlist, repo_root=None)
    check(
        "allow-list: hit matches entry for same file passes",
        len(result["violations"]) == 0 and len(result["allowed"]) == 1,
        repr((result["violations"], result["allowed"])),
    )

    file_texts_other = {"g.cpp": "double sd = std::sqrt(var);\n"}
    result = run_audit(file_texts_other, allowlist, repo_root=None)
    check(
        "allow-list: same text in another file fails",
        len(result["violations"]) == 1,
        repr(result["violations"]),
    )

    allowlist_empty_just = "f.cpp | std::sqrt\\(var\\) | \n"
    result = run_audit({"f.cpp": "double sd = std::sqrt(var);\n"}, allowlist_empty_just, repo_root=None)
    check(
        "allow-list: empty justification fails",
        len(result["malformed"]) == 1,
        repr(result["malformed"]),
    )

    allowlist_stale = "f.cpp | std::sqrt\\(var\\) | date arithmetic (synthetic test entry)\n"
    result = run_audit({"f.cpp": "// no statistical arithmetic here\n"}, allowlist_stale, repo_root=None)
    check(
        "allow-list: entry matching no hit is reported stale",
        len(result["stale"]) == 1,
        repr(result["stale"]),
    )

    if failures:
        print("SELF-TEST FAILURES:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    print(f"self-test: {12} positive/negative category checks + 4 allow-list checks passed")
    return 0


def main():
    if "--self-test" in sys.argv[1:]:
        return self_test()
    return run_real_audit()


if __name__ == "__main__":
    sys.exit(main())
