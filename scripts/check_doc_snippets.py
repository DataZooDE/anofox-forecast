#!/usr/bin/env python3
"""scripts/check_doc_snippets.py

Permanent docs/skills SQL snippet + function-name checker for anofox_forecast
(Phase 18 plan 18-04, D-06). Supersedes the per-phase phase-local forks at
.planning/phases/{10,12,13,14,15,16,17}-*/snippets/run_snippets.py (Phases 10-17) --
those stay on disk as history (never edited or deleted by this script) but are no
longer maintained; this script is the single permanent tool going forward.

Usage:
    python3 scripts/check_doc_snippets.py [FILE ...]
    python3 scripts/check_doc_snippets.py --changed-since REF
    python3 scripts/check_doc_snippets.py --out PATH [FILE ...]
    python3 scripts/check_doc_snippets.py --duckdb CLI --extension PATH.duckdb_extension

Default scan (no FILE args, no --changed-since): README.md, every docs/**/*.md and
every plugins/anofox-forecast/skills/**/*.md -- every ```sql fence in each is executed
(no BASE-commit diffing, unlike the phase-local forks: this is a full sweep by
default). Positional FILE args restrict the scan to exactly those files (still every
```sql fence within them). `--changed-since REF` restores the old changed-files-only
selection (files changed relative to REF, still every fence within each selected
file).

Each selected ```sql block is executed against ./build/release/duckdb (binary
missing: exit 2 with a message) in an in-memory database (`:memory:`), with a fresh
temporary directory as both the process cwd and HOME (removed afterward) and
`SET autoinstall_known_extensions = false;` prepended -- never touches the real repo
tree, the real extension cache, or the network. A block containing `INSTALL`, an
`http://`/`https://`/`s3://` URL, or a remote `ATTACH` is never executed at all
(SKIP-NETWORK).

Also collects every `_?ts_[a-z0-9_]+`-shaped identifier mentioned (a) anywhere in a
SKILL.md's frontmatter `description:` block (between the first two `---` lines), and
(b) inside a backtick code span or immediately before `(` (call syntax) anywhere in
any scanned markdown body -- and resolves each against the registered function
surface (`SELECT DISTINCT function_name FROM duckdb_functions()`, queried once against
the built extension). An unresolved name is a FAIL ("unknown function name <name>")
unless listed in scripts/doc_snippets/removed_names.txt.

Status per block: PASS, FAIL, EXPECTED-ERROR (keyed in
scripts/doc_snippets/expected_errors.txt -- the block must still fail; a listed block
that passes is a FAIL "expected error but passed"), SYNOPSIS (keyed in
scripts/doc_snippets/synopsis.txt -- narrative placeholder, never executed),
SKIP-NETWORK (network-looking block, never executed), SKIP-VERSION (a block whose first
line is `-- DuckDB 2.0+`, run only when the binary is DuckDB 2.0 or later, so the same docs
validate against a v1.5.6 build; on 2.0 such a block runs and counts like any other).

On a default full scan (no FILE args, no --changed-since), a stale
expected_errors.txt / synopsis.txt key (its file or its first line no longer exists
anywhere in the scanned doc set) or a stale removed_names.txt entry (the name is now a
registered function, or is no longer mentioned anywhere in the scanned doc set) is
reported as a STALE row.

Writes a TSV (columns: status, file, line, first SQL line, detail) to --out PATH
(default build/doc-snippets.tsv). Prints a summary line:
  check_doc_snippets: P PASS, F FAIL, E EXPECTED-ERROR, S SYNOPSIS, N SKIP-NETWORK, T STALE
Exit codes: 0 clean; 1 if FAIL or STALE count is non-zero; 2 if the duckdb binary is
missing or the registered-function-name query itself fails.

Plugin version agreement (default full scan only): plugins/anofox-forecast/.claude-plugin/
plugin.json "version", every SKILL.md frontmatter `version:` and every SKILL.md
`**Extension:** `anofox_forecast` vX (Rust crate `anofox-forecast` vY)` header line must
carry the newest released `## [YYYY.MM.DD]` heading in CHANGELOG.md (X) and the
anofox-forecast crate version in Cargo.lock (Y). A mismatch is a FAIL row. The skills sat
at 2026.09.26 / v0.16.2 through three releases before this check existed. Fix with
`make bump-plugin` (scripts/bump_plugin_version.sh) in the release commit.

CI mode: `--duckdb CLI --extension PATH` runs every block in the given (official) DuckDB
CLI and rewrites `LOAD anofox_forecast` to `LOAD 'PATH'`, so the packaged
.duckdb_extension is what gets checked, not the statically linked build/release/duckdb.
MainDistributionPipeline.yml runs it that way after each build (.github/workflows/
_doc_snippets.yml), and each track's deploy waits for it, like the smoke test.
Locally it still needs a built `build/release/duckdb` (`make release && make check-docs`).
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = subprocess.run(
    ["git", "rev-parse", "--show-toplevel"], cwd=SCRIPT_DIR, capture_output=True, text=True, check=True
).stdout.strip()

SUPPORT_DIR = os.path.join(SCRIPT_DIR, "doc_snippets")
FIXTURES_FILE = os.path.join(SUPPORT_DIR, "fixtures.sql")
EXPECTED_ERRORS_FILE = os.path.join(SUPPORT_DIR, "expected_errors.txt")
SYNOPSIS_FILE = os.path.join(SUPPORT_DIR, "synopsis.txt")
REMOVED_NAMES_FILE = os.path.join(SUPPORT_DIR, "removed_names.txt")
DUCKDB_BIN = os.path.join(REPO_ROOT, "build", "release", "duckdb")
EXTENSION_PATH = None  # --extension: LOAD this file instead of the linked-in extension
LOAD_EXT_RE = re.compile(r"\bLOAD\s+anofox_forecast\b", re.IGNORECASE)
PLUGIN_DIR = os.path.join("plugins", "anofox-forecast")
RELEASE_RE = re.compile(r"^## \[(\d{4}\.\d{1,2}\.\d{1,2})\]", re.M)
VERSION_FM_RE = re.compile(r"^version:\s*(\S+)\s*$", re.M)
HEADER_RE = re.compile(
    r"^\*\*Extension:\*\* `anofox_forecast` v([0-9.]+) \(Rust crate `anofox-forecast` v([0-9.]+)\)",
    re.M,
)
CRATE_LOCK_RE = re.compile(r'^name = "anofox-forecast"\nversion = "([^"]+)"', re.M)
DEFAULT_OUT = os.path.join(REPO_ROOT, "build", "doc-snippets.tsv")

DEFAULT_SCAN_DIRS = ["docs", os.path.join("plugins", "anofox-forecast", "skills")]

FENCE_RE = re.compile(r"^```[ \t]*([A-Za-z0-9_+-]*)[ \t]*$")
SETUP_RE = re.compile(
    r"\b(CREATE\s+(OR\s+REPLACE\s+)?(TEMP\s+|TEMPORARY\s+)?(TABLE|VIEW|MACRO)|INSERT)\b",
    re.IGNORECASE,
)
CREATE_NAME_RE = re.compile(
    r"\bCREATE\s+(?:OR\s+REPLACE\s+)?(?:TEMP\s+|TEMPORARY\s+)?(?:TABLE|VIEW|MACRO)\s+"
    r"(?:IF\s+NOT\s+EXISTS\s+)?([A-Za-z_][A-Za-z0-9_]*)",
    re.IGNORECASE,
)
INSERT_NAME_RE = re.compile(r"\bINSERT\s+INTO\s+([A-Za-z_][A-Za-z0-9_]*)", re.IGNORECASE)
PLACEHOLDER_ELLIPSIS_RE = re.compile(r"(?<!\d)\.\.\.(?!\d)")
NETWORK_RE = re.compile(r"\bINSTALL\b|https?://|s3://", re.IGNORECASE)

IDENT_RE = re.compile(r"(_?ts_[a-z0-9_]+)")
BACKTICK_IDENT_RE = re.compile(r"`(_?ts_[a-z0-9_]+)`")
CALL_IDENT_RE = re.compile(r"\b(_?ts_[a-z0-9_]+)\s*\(")


def read_text(abs_path):
    with open(abs_path, encoding="utf-8") as f:
        return f.read()


def git_show(ref, path):
    result = subprocess.run(
        ["git", "show", f"{ref}:{path}"], cwd=REPO_ROOT, capture_output=True, text=True
    )
    if result.returncode != 0:
        return None
    return result.stdout


def changed_md_files(ref):
    result = subprocess.run(
        ["git", "diff", "--name-only", ref, "--", "README.md"] + DEFAULT_SCAN_DIRS,
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    files = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    return [f for f in files if f.endswith(".md") and os.path.exists(os.path.join(REPO_ROOT, f))]


def default_scan_files():
    files = []
    if os.path.exists(os.path.join(REPO_ROOT, "README.md")):
        files.append("README.md")
    for scan_dir in DEFAULT_SCAN_DIRS:
        abs_dir = os.path.join(REPO_ROOT, scan_dir)
        if not os.path.isdir(abs_dir):
            continue
        for root, dirs, fnames in os.walk(abs_dir):
            dirs.sort()
            for fn in sorted(fnames):
                if fn.endswith(".md"):
                    files.append(os.path.relpath(os.path.join(root, fn), REPO_ROOT))
    return files


def extract_sql_blocks(text):
    """Return list of (block_text, start_line_idx) for ```sql fenced blocks."""
    if text is None:
        return []
    lines = text.splitlines()
    blocks = []
    i = 0
    n = len(lines)
    while i < n:
        m = FENCE_RE.match(lines[i].strip())
        if m and m.group(1).lower() == "sql":
            start = i
            body_lines = []
            i += 1
            while i < n and lines[i].strip() != "```":
                body_lines.append(lines[i])
                i += 1
            blocks.append(("\n".join(body_lines), start))
        i += 1
    return blocks


def first_non_comment_line(block_text):
    for line in block_text.splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("--"):
            return stripped
    for line in block_text.splitlines():
        if line.strip():
            return line.strip()
    return ""


def load_key_set(path):
    if not os.path.exists(path):
        return set()
    with open(path) as f:
        return {line.rstrip("\n") for line in f if line.strip()}


def load_removed_names(path):
    """name<TAB>reason, one per line; blank lines and '#'-comment lines ignored."""
    names = {}
    if not os.path.exists(path):
        return names
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.strip() or line.strip().startswith("#"):
                continue
            parts = line.split("\t", 1)
            name = parts[0].strip()
            reason = parts[1].strip() if len(parts) > 1 else ""
            if name:
                names[name] = reason
    return names


def strip_sql_comments(text):
    return "\n".join(line.split("--", 1)[0] for line in text.splitlines())


def setup_created_name(block_text):
    m = CREATE_NAME_RE.search(block_text)
    if m:
        return m.group(1)
    m = INSERT_NAME_RE.search(block_text)
    if m:
        return m.group(1)
    return None


def self_created_names(text):
    return {n for n in CREATE_NAME_RE.findall(text) if n}


def select_required_setup(target_text, setup_accum, fixture_names=frozenset()):
    """Transitive closure of setup blocks (in doc order) actually referenced by
    target_text, excluding names the target creates itself and names already
    satisfied by fixtures.sql."""
    shadowed = self_created_names(strip_sql_comments(target_text)) | set(fixture_names)
    needed_text = [strip_sql_comments(target_text)]
    included = []
    included_names = set()
    changed = True
    while changed:
        changed = False
        combined = "\n".join(needed_text)
        for name, block_text in setup_accum:
            if name in included_names or name in shadowed:
                continue
            if re.search(r"\b" + re.escape(name) + r"\b", combined):
                included.append(block_text)
                included_names.add(name)
                needed_text.append(strip_sql_comments(block_text))
                changed = True
    order = {id(bt): i for i, (_, bt) in enumerate(setup_accum)}
    included.sort(key=lambda bt: order.get(id(bt), 0))
    return included


def first_error_line(stderr, stdout):
    for stream in (stderr, stdout):
        for line in stream.splitlines():
            if line.strip():
                return line.strip()[:200]
    return ""


def run_block_safe(sql_text, timeout=120):
    """Execute sql_text against DUCKDB_BIN in :memory:, with a fresh temp dir as
    both cwd and HOME (removed afterward), autoinstall disabled."""
    tmp_dir = tempfile.mkdtemp(prefix="doc_snippets_")
    try:
        env = dict(os.environ)
        env["HOME"] = tmp_dir
        full_sql = "SET autoinstall_known_extensions = false;\n" + sql_text
        if EXTENSION_PATH:
            full_sql = LOAD_EXT_RE.sub("LOAD '" + EXTENSION_PATH.replace("'", "''") + "'", full_sql)
        try:
            result = subprocess.run(
                [DUCKDB_BIN, "-unsigned", "-bail", "-list", ":memory:"],
                input=full_sql,
                cwd=tmp_dir,
                capture_output=True,
                text=True,
                timeout=timeout,
                env=env,
            )
            return result.returncode, result.stdout, result.stderr
        except subprocess.TimeoutExpired:
            return 124, "", f"TIMEOUT after {timeout}s"
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


def query_registered_function_names():
    rc, out, err = run_block_safe(
        "LOAD anofox_forecast;\nLOAD json;\nSELECT DISTINCT function_name FROM duckdb_functions();\n"
    )
    if rc != 0:
        print(
            f"ERROR: failed to query duckdb_functions(): {(err.strip() or out.strip())[:500]}",
            file=sys.stderr,
        )
        sys.exit(2)
    return {line.strip() for line in out.splitlines() if line.strip()}


def query_duckdb_major():
    rc, out, err = run_block_safe("SELECT version();\n")
    m = re.search(r"v(\d+)\.", out or "")
    return int(m.group(1)) if rc == 0 and m else 0


V2_ONLY_RE = re.compile(r"\A\s*--\s*DuckDB 2\.0\+")


def collect_name_mentions(rel_path, text):
    """Return list of (name, line_no) identifier mentions to check against the
    registered function surface. SKILL.md frontmatter descriptions: every bare
    `_?ts_[a-z0-9_]+` token. Everywhere else (incl. SKILL.md bodies): only backtick
    code spans and call-syntax (`name(`)."""
    mentions = []
    lines = text.splitlines()
    frontmatter_end = 0
    is_skill = rel_path.endswith("SKILL.md")
    if is_skill and lines and lines[0].strip() == "---":
        for i in range(1, len(lines)):
            if lines[i].strip() == "---":
                frontmatter_end = i
                break
        for i in range(1, frontmatter_end):
            for m in IDENT_RE.finditer(lines[i]):
                mentions.append((m.group(1), i + 1))
    for i, line in enumerate(lines):
        if is_skill and i < frontmatter_end:
            continue
        for m in BACKTICK_IDENT_RE.finditer(line):
            mentions.append((m.group(1), i + 1))
        for m in CALL_IDENT_RE.finditer(line):
            mentions.append((m.group(1), i + 1))
    return mentions


def plugin_version_failures():
    """(file, detail) for every plugin/skill version that disagrees with CHANGELOG/Cargo.lock."""
    releases = RELEASE_RE.findall(read_text(os.path.join(REPO_ROOT, "CHANGELOG.md")))
    want = releases[0] if releases else None
    m = CRATE_LOCK_RE.search(read_text(os.path.join(REPO_ROOT, "Cargo.lock")))
    want_crate = m.group(1) if m else None
    failures = []
    manifest = os.path.join(PLUGIN_DIR, ".claude-plugin", "plugin.json")

    got = json.loads(read_text(os.path.join(REPO_ROOT, manifest))).get("version")
    if got != want:
        failures.append((manifest, f"version {got} != newest CHANGELOG release {want}"))
    skills_root = os.path.join(REPO_ROOT, PLUGIN_DIR, "skills")
    for name in sorted(os.listdir(skills_root)):
        path = os.path.join(skills_root, name, "SKILL.md")
        if not os.path.exists(path):
            continue
        rel = os.path.relpath(path, REPO_ROOT)
        text = read_text(path)
        fm = VERSION_FM_RE.search(text.split("\n---", 1)[0])
        if (fm.group(1) if fm else None) != want:
            failures.append((rel, f"version: {fm.group(1) if fm else None} != newest CHANGELOG release {want}"))
        h = HEADER_RE.search(text)
        if not h:
            failures.append((rel, "no '**Extension:** `anofox_forecast` vX (Rust crate `anofox-forecast` vY)' header"))
            continue
        if h.group(1) != want:
            failures.append((rel, f"header v{h.group(1)} != newest CHANGELOG release {want}"))
        if h.group(2) != want_crate:
            failures.append((rel, f"header crate v{h.group(2)} != Cargo.lock anofox-forecast {want_crate}"))
    return failures


def main():
    global DUCKDB_BIN, EXTENSION_PATH
    args = sys.argv[1:]
    files_arg = []
    changed_since = None
    out_path = DEFAULT_OUT
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--changed-since":
            i += 1
            if i >= len(args):
                print("ERROR: --changed-since requires a REF argument", file=sys.stderr)
                return 1
            changed_since = args[i]
        elif a == "--out":
            i += 1
            if i >= len(args):
                print("ERROR: --out requires a PATH argument", file=sys.stderr)
                return 1
            out_path = args[i]
        elif a in ("--duckdb", "--extension"):
            i += 1
            if i >= len(args):
                print(f"ERROR: {a} requires a PATH argument", file=sys.stderr)
                return 1
            if a == "--duckdb":
                DUCKDB_BIN = os.path.abspath(args[i])
            else:
                EXTENSION_PATH = os.path.abspath(args[i])
        else:
            files_arg.append(a)
        i += 1

    if not os.path.exists(DUCKDB_BIN):
        print(f"ERROR: duckdb binary not found at {DUCKDB_BIN} -- build it first (make release)", file=sys.stderr)
        return 2

    is_full_scan = not files_arg and not changed_since

    if files_arg:
        files = files_arg
    elif changed_since:
        files = changed_md_files(changed_since)
    else:
        files = default_scan_files()

    fixtures = ""
    if os.path.exists(FIXTURES_FILE):
        fixtures = read_text(FIXTURES_FILE)
    fixture_names = set(CREATE_NAME_RE.findall(strip_sql_comments(fixtures)))

    expected_errors = load_key_set(EXPECTED_ERRORS_FILE)
    synopsis_keys = load_key_set(SYNOPSIS_FILE)
    removed_names = load_removed_names(REMOVED_NAMES_FILE)
    registered_names = query_registered_function_names()
    duckdb_major = query_duckdb_major()

    rows = []  # (status, file, line, first_sql_line, detail)
    status_counts = {"PASS": 0, "FAIL": 0, "EXPECTED-ERROR": 0, "SYNOPSIS": 0, "SKIP-NETWORK": 0, "SKIP-VERSION": 0, "STALE": 0}

    all_block_keys = set()
    all_mentioned_names = set()
    seen_unknown = set()  # (file, name) already reported as FAIL

    for rel_path in files:
        abs_path = os.path.join(REPO_ROOT, rel_path)
        if not os.path.exists(abs_path):
            continue
        text = read_text(abs_path)

        # --- Function-name resolution (frontmatter + backtick/call-syntax bodies) ---
        for name, line_no in collect_name_mentions(rel_path, text):
            all_mentioned_names.add(name)
            if name in registered_names or name in removed_names:
                continue
            dedup_key = (rel_path, name)
            if dedup_key in seen_unknown:
                continue
            seen_unknown.add(dedup_key)
            rows.append(("FAIL", rel_path, str(line_no), "", f"unknown function name {name}"))
            status_counts["FAIL"] += 1

        # --- SQL fence execution ---
        blocks = extract_sql_blocks(text)
        setup_accum = []  # list of (created_name, block_text)

        for block_text, start_line in blocks:
            key = f"{rel_path}\t{first_non_comment_line(block_text)}"
            all_block_keys.add(key)
            is_placeholder = bool(PLACEHOLDER_ELLIPSIS_RE.search(block_text))
            is_setup_only = bool(SETUP_RE.search(block_text)) and not is_placeholder
            first_line_trunc = first_non_comment_line(block_text)[:100]

            if NETWORK_RE.search(block_text):
                status = "SKIP-NETWORK"
                detail = "network-looking block (INSTALL/URL), not executed"
            elif V2_ONLY_RE.search(block_text) and duckdb_major < 2:
                status = "SKIP-VERSION"
                detail = "DuckDB 2.0+ block, binary is older"
            elif key in synopsis_keys:
                status = "SYNOPSIS"
                detail = "narrative placeholder, not executed"
            else:
                required_setup = select_required_setup(block_text, setup_accum, fixture_names)
                full_sql = fixtures
                for setup_sql in required_setup:
                    full_sql += "\n" + setup_sql
                full_sql += "\n" + block_text

                rc, stdout, stderr = run_block_safe(full_sql)
                raw_pass = rc == 0

                if raw_pass:
                    if key in expected_errors:
                        status = "FAIL"
                        detail = "expected error but passed"
                    else:
                        status = "PASS"
                        detail = ""
                else:
                    if key in expected_errors:
                        status = "EXPECTED-ERROR"
                        detail = ""
                    else:
                        status = "FAIL"
                        detail = first_error_line(stderr, stdout)

            rows.append((status, rel_path, str(start_line + 1), first_line_trunc, detail))
            status_counts[status] = status_counts.get(status, 0) + 1

            if is_setup_only:
                name = setup_created_name(block_text)
                if name:
                    setup_accum.append((name, block_text))

    # --- Stale support-file entries (default full scan only) ---
    if is_full_scan:
        for key in sorted(expected_errors):
            if key not in all_block_keys:
                file_part = key.split("\t", 1)[0]
                rows.append(("STALE", file_part, "", "", f"stale expected_errors.txt entry: {key}"))
                status_counts["STALE"] += 1
        for key in sorted(synopsis_keys):
            if key not in all_block_keys:
                file_part = key.split("\t", 1)[0]
                rows.append(("STALE", file_part, "", "", f"stale synopsis.txt entry: {key}"))
                status_counts["STALE"] += 1
        for name in sorted(removed_names):
            if name in registered_names:
                rows.append(("STALE", "scripts/doc_snippets/removed_names.txt", "", "", f"{name} is registered (no longer removed)"))
                status_counts["STALE"] += 1
            elif name not in all_mentioned_names:
                rows.append(("STALE", "scripts/doc_snippets/removed_names.txt", "", "", f"{name} is no longer mentioned anywhere"))
                status_counts["STALE"] += 1

        for rel, detail in plugin_version_failures():
            rows.append(("FAIL", rel, "", "", detail))
            status_counts["FAIL"] += 1

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w") as f:
        for row in rows:
            f.write("\t".join(row) + "\n")

    p = status_counts.get("PASS", 0)
    fcount = status_counts.get("FAIL", 0)
    e = status_counts.get("EXPECTED-ERROR", 0)
    s = status_counts.get("SYNOPSIS", 0)
    n = status_counts.get("SKIP-NETWORK", 0)
    v = status_counts.get("SKIP-VERSION", 0)
    t = status_counts.get("STALE", 0)
    print(f"check_doc_snippets: {p} PASS, {fcount} FAIL, {e} EXPECTED-ERROR, {s} SYNOPSIS, {n} SKIP-NETWORK, {v} SKIP-VERSION, {t} STALE")

    return 1 if (fcount or t) else 0


if __name__ == "__main__":
    sys.exit(main())
