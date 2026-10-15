#!/usr/bin/env python3
"""Correctness-suite inventory gate (Phase 18, 18-02, TEST-03).

Extracts every backticked `_?ts_*` identifier from the CHANGELOG.md `## [Unreleased]`
section (i.e. every function this milestone touched), intersects it with the function
names actually registered by the built extension (`duckdb_functions()`), and checks that
each surviving (registered) name is exercised on a non-comment line of at least one
`test/sql/correctness/*.test` file.

Usage:
    python3 scripts/check_correctness_inventory.py

Exit codes:
    0 - every registered, CHANGELOG-mentioned function is covered by a correctness file
    1 - at least one registered function has no correctness-file coverage
    2 - build/release/duckdb is missing (nothing to query)

Prints a per-function table to stdout, one `MISSING <name>` line per uncovered
registered function (for easy grepping), and informational "not registered: ..." lines
for names the CHANGELOG mentions that duckdb_functions() does not know about (removed,
renamed, or a prose fragment that merely looked like an identifier).
"""
import os
import re
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = subprocess.run(
    ["git", "rev-parse", "--show-toplevel"], cwd=SCRIPT_DIR, capture_output=True, text=True, check=True
).stdout.strip()

DUCKDB_BIN = os.path.join(REPO_ROOT, "build", "release", "duckdb")
CHANGELOG_PATH = os.path.join(REPO_ROOT, "CHANGELOG.md")
CORRECTNESS_DIR = os.path.join(REPO_ROOT, "test", "sql", "correctness")

IDENT_RE = re.compile(r"`(_?ts_[a-z0-9_]+?)(?:\(\))?`")

# Functions the CHANGELOG's Unreleased section does not backtick explicitly, but which
# were fixed this milestone and must still be checked for correctness-suite coverage.
# Empty unless a cross-check against phase SUMMARYs finds one. Format: list[str].
EXTRA = []


def read_unreleased_section():
    with open(CHANGELOG_PATH, encoding="utf-8") as f:
        lines = f.readlines()
    start = None
    end = len(lines)
    for i, line in enumerate(lines):
        if line.startswith("## [Unreleased]"):
            start = i
            continue
        if start is not None and i > start and line.startswith("## ["):
            end = i
            break
    if start is None:
        print("ERROR: no '## [Unreleased]' heading found in CHANGELOG.md", file=sys.stderr)
        sys.exit(2)
    return lines[start:end]


def extract_changelog_identifiers(section_lines):
    names = set()
    for line in section_lines:
        for m in IDENT_RE.finditer(line):
            names.add(m.group(1))
    names.update(EXTRA)
    return names


def get_registered_names():
    if not os.path.isfile(DUCKDB_BIN):
        print(f"ERROR: {DUCKDB_BIN} not found -- build the extension first (make release)", file=sys.stderr)
        sys.exit(2)
    result = subprocess.run(
        [DUCKDB_BIN, "-csv", "-noheader", "-c", "SELECT DISTINCT function_name FROM duckdb_functions();"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"ERROR: querying duckdb_functions() failed: {result.stderr}", file=sys.stderr)
        sys.exit(2)
    return set(line.strip() for line in result.stdout.splitlines() if line.strip())


def correctness_files():
    if not os.path.isdir(CORRECTNESS_DIR):
        return []
    return sorted(
        os.path.join(CORRECTNESS_DIR, f)
        for f in os.listdir(CORRECTNESS_DIR)
        if f.endswith(".test")
    )


def mentions_on_noncomment_line(filepath, name):
    pattern = re.compile(r"\b" + re.escape(name) + r"\b")
    with open(filepath, encoding="utf-8") as f:
        for line in f:
            stripped = line.strip()
            if stripped.startswith("#"):
                continue
            if pattern.search(line):
                return True
    return False


def main():
    section_lines = read_unreleased_section()
    changelog_names = extract_changelog_identifiers(section_lines)
    registered_names = get_registered_names()
    files = correctness_files()

    covered_names = changelog_names & registered_names
    not_registered = sorted(changelog_names - registered_names)

    coverage = {}
    for name in sorted(covered_names):
        hits = [f for f in files if mentions_on_noncomment_line(f, name)]
        coverage[name] = hits

    print("name,covered,files")
    missing_count = 0
    for name in sorted(coverage):
        hits = coverage[name]
        rel_hits = ";".join(os.path.relpath(h, REPO_ROOT) for h in hits)
        print(f"{name},{bool(hits)},{rel_hits}")
        if not hits:
            missing_count += 1

    for name in sorted(coverage):
        if not coverage[name]:
            print(f"MISSING {name}")

    for name in not_registered:
        print(f"not registered: {name} (removed, renamed or a fragment)", file=sys.stderr)

    print(
        f"\nSummary: {len(coverage)} CHANGELOG-mentioned registered functions, "
        f"{missing_count} missing correctness coverage, {len(not_registered)} not registered.",
        file=sys.stderr,
    )

    sys.exit(1 if missing_count > 0 else 0)


if __name__ == "__main__":
    main()
