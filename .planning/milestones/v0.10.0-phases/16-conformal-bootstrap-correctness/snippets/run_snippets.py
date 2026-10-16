#!/usr/bin/env python3
"""Snippet harness (phase-local copy for Phase 16 plan 16-03, forked from Phase 14's
copy of the quick task 261009-d02 docs/README/skills accuracy pass harness).

Extracts ```sql fenced code blocks from markdown docs, determines which blocks changed
relative to a recorded BASE commit, and executes each selected block against the built
DuckDB extension shell to prove docs examples still work.

Usage:
    python3 run_snippets.py [--all] [--out PATH] [FILE ...]

With no FILE args, checks every *.md file changed (relative to BASE, against the working
tree) under README.md, docs/, plugins/, .claude/CLAUDE.md.

Writes pass-list.tsv (overwrite) next to this script by default, or the path given via
--out (so each docs plan can write its own pass list without clobbering a sibling run).
Exit code 1 if any FAIL row, else 0.
"""
import os
import re
import subprocess
import sys

SNIP_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = subprocess.run(
    ["git", "rev-parse", "--show-toplevel"], cwd=SNIP_DIR, capture_output=True, text=True, check=True
).stdout.strip()

BASE_FILE = os.path.join(SNIP_DIR, "BASE")
FIXTURES_FILE = os.path.join(SNIP_DIR, "fixtures.sql")
PASS_LIST_FILE = os.path.join(SNIP_DIR, "pass-list.tsv")
EXPECTED_ERRORS_FILE = os.path.join(SNIP_DIR, "expected_errors.txt")
SYNOPSIS_FILE = os.path.join(SNIP_DIR, "synopsis.txt")
DUCKDB_BIN = os.path.join(REPO_ROOT, "build", "release", "duckdb")

DEFAULT_SCAN_PATHS = ["README.md", "docs", "plugins", ".claude/CLAUDE.md"]

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
ERROR_WORDS_RE = re.compile(
    r"\b(error|errors|fail|fails|failing|crash|crashes|crashed|segfault|reject|rejects|"
    r"rejected|invalid|unsupported|not (currently )?supported|will not work|does not work|"
    r"binder error|not registered)\b",
    re.IGNORECASE,
)


def read_base():
    with open(BASE_FILE) as f:
        return f.read().strip()


def git_show(ref, path):
    """Return file content at ref:path, or None if it doesn't exist there."""
    result = subprocess.run(
        ["git", "show", f"{ref}:{path}"], cwd=REPO_ROOT, capture_output=True, text=True
    )
    if result.returncode != 0:
        return None
    return result.stdout


def changed_md_files(base):
    result = subprocess.run(
        ["git", "diff", "--name-only", base, "--"] + DEFAULT_SCAN_PATHS,
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    files = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    return [f for f in files if f.endswith(".md")]


def extract_sql_blocks(text):
    """Return list of (block_text, start_line_idx, context_before) for ```sql fenced blocks."""
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
            # i now at closing fence (or EOF)
            block_text = "\n".join(body_lines)
            context_before = "\n".join(lines[max(0, start - 15):start])
            blocks.append((block_text, start, context_before))
        i += 1
    return blocks


def normalize_block(block_text):
    """Strip trailing whitespace per line for identity comparison."""
    return "\n".join(line.rstrip() for line in block_text.splitlines())


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


def run_block(sql_text, timeout=120):
    try:
        result = subprocess.run(
            [DUCKDB_BIN, "-unsigned", "-bail", ":memory:"],
            input=sql_text,
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as e:
        return 124, "", f"TIMEOUT after {timeout}s"


def strip_sql_comments(text):
    """Remove -- line comments before dependency-name matching, so an English word in a
    comment (e.g. "-- Learn calibration profile...") can't false-match a setup block's
    created table name (e.g. a table literally named `calibration`)."""
    return "\n".join(line.split("--", 1)[0] for line in text.splitlines())


def setup_created_name(block_text):
    """Return the table/view/macro name a setup block creates (or inserts into), if any."""
    m = CREATE_NAME_RE.search(block_text)
    if m:
        return m.group(1)
    m = INSERT_NAME_RE.search(block_text)
    if m:
        return m.group(1)
    return None


def self_created_names(text):
    """All table/view/macro names the block itself creates (multi-statement blocks can
    both CREATE and later consume their own table within one fence)."""
    names = set(CREATE_NAME_RE.findall(text))
    # findall on a group-only pattern returns the captured group directly when there's
    # exactly one group; CREATE_NAME_RE has one capturing group so this is fine.
    return {n for n in names if n}


def select_required_setup(target_text, setup_accum, fixture_names=frozenset()):
    """Transitive closure: setup blocks (in doc order) whose created name is referenced,
    directly or indirectly, by target_text. setup_accum is a list of (name, block_text)
    in file order; returns the subset (in original order) actually needed.

    Names the target block creates itself (multi-statement blocks that CREATE then
    consume their own table) are excluded from matching -- otherwise an unrelated earlier
    section that happens to reuse the same illustrative table name (e.g. two different
    "calib" tables in two different walkthroughs) gets wrongly pulled in as a dependency.

    Names already provided by fixtures.sql are also excluded: fixtures.sql loads before
    any doc-local setup, so a doc section that happens to reuse a fixture's table name for
    its own unrelated illustrative pipeline (e.g. a generic "clean" table) must not have
    that doc-local chain pulled in -- fixtures.sql already satisfies the reference, and the
    doc-local chain may depend on tables that were never actually created (e.g. a narrative
    "raw" source table that only ever appears as an assumed pre-existing input)."""
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
    # Preserve original file order among included blocks.
    order = {id(bt): i for i, (_, bt) in enumerate(setup_accum)}
    included.sort(key=lambda bt: order.get(id(bt), 0))
    return included


def first_error_line(stderr, stdout):
    for stream in (stderr, stdout):
        for line in stream.splitlines():
            if line.strip():
                return line.strip()[:200]
    return ""


def main():
    args = sys.argv[1:]
    all_flag = False
    files_arg = []
    out_path = PASS_LIST_FILE
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--all":
            all_flag = True
        elif a == "--out":
            i += 1
            if i >= len(args):
                print("ERROR: --out requires a PATH argument", file=sys.stderr)
                return 1
            out_path = args[i]
        else:
            files_arg.append(a)
        i += 1

    base = read_base()

    if files_arg:
        files = files_arg
    else:
        files = changed_md_files(base)

    fixtures = ""
    if os.path.exists(FIXTURES_FILE):
        with open(FIXTURES_FILE) as f:
            fixtures = f.read()
    fixture_names = set(CREATE_NAME_RE.findall(strip_sql_comments(fixtures)))

    expected_errors = load_key_set(EXPECTED_ERRORS_FILE)
    synopsis_keys = load_key_set(SYNOPSIS_FILE)

    rows = []
    status_counts = {"PASS": 0, "EXPECTED-ERROR": 0, "SYNOPSIS": 0, "FAIL": 0}
    any_fail = False

    for rel_path in files:
        abs_path = os.path.join(REPO_ROOT, rel_path)
        if not os.path.exists(abs_path):
            continue
        with open(abs_path, encoding="utf-8") as f:
            working_text = f.read()

        working_blocks = extract_sql_blocks(working_text)
        base_text = git_show(base, rel_path)
        base_blocks = extract_sql_blocks(base_text)
        base_norms = {normalize_block(bt) for bt, _, _ in base_blocks}

        # Pre-compute setup SQL accumulated from earlier blocks in doc order, tagged with
        # the name each one creates (for dependency-based selection -- see
        # select_required_setup). Blindly concatenating EVERY prior setup block breaks
        # narrative docs where an early illustrative snippet references a table that is
        # only actually built later in the same file.
        setup_accum = []  # list of (created_name, block_text)
        pass_results_by_norm = {}  # norm -> did it PASS (for SYNOPSIS sibling check)

        for idx, (block_text, start_line, context_before) in enumerate(working_blocks):
            norm = normalize_block(block_text)
            is_changed = norm not in base_norms
            # Skip non-executable placeholder blocks (e.g. "CREATE TABLE x AS ...;") from
            # setup accumulation -- a literal ellipsis marks illustrative pseudo-SQL that
            # would poison every later block in the file if blindly concatenated.
            is_placeholder = bool(PLACEHOLDER_ELLIPSIS_RE.search(block_text))
            is_setup_only = bool(SETUP_RE.search(block_text)) and not is_placeholder

            should_run = all_flag or is_changed

            key = f"{rel_path}\t{first_non_comment_line(block_text)}"

            if should_run:
                required_setup = select_required_setup(block_text, setup_accum, fixture_names)
                full_sql = fixtures
                for setup_sql in required_setup:
                    full_sql += "\n" + setup_sql
                full_sql += "\n" + block_text

                rc, stdout, stderr = run_block(full_sql)
                raw_pass = rc == 0

                if raw_pass:
                    status = "PASS"
                elif key in expected_errors and ERROR_WORDS_RE.search(context_before):
                    status = "EXPECTED-ERROR"
                elif key in synopsis_keys:
                    status = "SYNOPSIS"
                else:
                    status = "FAIL"

                if status == "FAIL":
                    any_fail = True

                err_line = "" if status == "PASS" else first_error_line(stderr, stdout)
                first_line_trunc = first_non_comment_line(block_text)[:100]
                rows.append((rel_path, str(idx), status, first_line_trunc, err_line))
                status_counts[status] = status_counts.get(status, 0) + 1
                pass_results_by_norm[norm] = raw_pass

            # Accumulate setup blocks for later blocks in this file (always, not just when run)
            if is_setup_only:
                name = setup_created_name(block_text)
                if name:
                    setup_accum.append((name, block_text))

        # Second pass: verify SYNOPSIS rows have a PASSing sibling in the same file.
        # (Best-effort: if no block in the file passed at all, downgrade SYNOPSIS rows to FAIL.)
        file_rows = [r for r in rows if r[0] == rel_path]
        any_pass_in_file = any(r[2] == "PASS" for r in file_rows)
        if not any_pass_in_file:
            for i, r in enumerate(rows):
                if r[0] == rel_path and r[2] == "SYNOPSIS":
                    rows[i] = (r[0], r[1], "FAIL", r[3], r[4] or "no passing sibling example in file")
                    status_counts["SYNOPSIS"] -= 1
                    status_counts["FAIL"] += 1
                    any_fail = True

    with open(out_path, "w") as f:
        for row in rows:
            f.write("\t".join(row) + "\n")

    for status in ("PASS", "EXPECTED-ERROR", "SYNOPSIS", "FAIL"):
        print(f"{status}: {status_counts.get(status, 0)}")

    return 1 if any_fail else 0


if __name__ == "__main__":
    sys.exit(main())
