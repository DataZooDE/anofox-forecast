#!/usr/bin/env python3
"""Snippet harness for the docs/README/skills accuracy pass (quick task 261009-d02).

Extracts ```sql fenced code blocks from markdown docs, determines which blocks changed
relative to a recorded BASE commit, and executes each selected block against the built
DuckDB extension shell to prove docs examples still work.

Usage:
    python3 run_snippets.py [--all] [FILE ...]

With no FILE args, checks every *.md file changed (relative to BASE, against the working
tree) under README.md, docs/, plugins/, .claude/CLAUDE.md.

Writes pass-list.tsv (overwrite) next to this script. Exit code 1 if any FAIL row, else 0.
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
SETUP_RE = re.compile(r"\b(CREATE\s+TABLE|CREATE\s+VIEW|CREATE\s+MACRO|INSERT)\b", re.IGNORECASE)
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
    for a in args:
        if a == "--all":
            all_flag = True
        else:
            files_arg.append(a)

    base = read_base()

    if files_arg:
        files = files_arg
    else:
        files = changed_md_files(base)

    fixtures = ""
    if os.path.exists(FIXTURES_FILE):
        with open(FIXTURES_FILE) as f:
            fixtures = f.read()

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

        # Pre-compute setup SQL accumulated from earlier blocks in doc order.
        setup_accum = []
        pass_results_by_norm = {}  # norm -> did it PASS (for SYNOPSIS sibling check)

        for idx, (block_text, start_line, context_before) in enumerate(working_blocks):
            norm = normalize_block(block_text)
            is_changed = norm not in base_norms
            is_setup_only = bool(SETUP_RE.search(block_text))

            should_run = all_flag or is_changed

            key = f"{rel_path}\t{first_non_comment_line(block_text)}"

            if should_run:
                full_sql = fixtures
                for setup_sql in setup_accum:
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
                setup_accum.append(block_text)

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

    with open(PASS_LIST_FILE, "w") as f:
        for row in rows:
            f.write("\t".join(row) + "\n")

    for status in ("PASS", "EXPECTED-ERROR", "SYNOPSIS", "FAIL"):
        print(f"{status}: {status_counts.get(status, 0)}")

    return 1 if any_fail else 0


if __name__ == "__main__":
    sys.exit(main())
