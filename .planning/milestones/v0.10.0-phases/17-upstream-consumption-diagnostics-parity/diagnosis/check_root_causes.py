#!/usr/bin/env python3
"""D-12 root-cause table integrity checker (Phase 17 plan 17-03).

Loads both r_forecast fixtures (air_passengers.json, simulated_series.json),
which share a single `tolerance` object (REF_TOLERANCE in
gen_forecast_reference.R), and checks the `root_causes` table and, at
--stage final, the `parity_checks` table's cause-integrity.

D-16 (user decision, 2026-10-15) carved out exactly ONE allowed AutoARIMA
interval known gap, air/auto_arima/AutoARIMA, cited by a crate-located
root_causes row (RC-05). RC-05 was RESOLVED by anofox-forecast 0.16.2
(upstream PR #230/#229, 18-05 gap closure): the AirPassengers AutoARIMA
interval is now at hard parity, so D-16's exception is retired. As of
18-05, NO AutoARIMA check may be `status = "known_gap"`, and no
AutoARIMA check may be cited by an *unresolved* crate/reference
root_causes row. A root_causes row is "resolved" when its `resolution`
field starts with `resolved:` (or, for extension-located rows, `fixed:`)
— such rows are historical record of a cause that no longer applies and
are exempt from `autoarima_gaps`. Any AutoARIMA check cited by an
unresolved crate/reference root_causes row, or carrying parity_checks
status "known_gap", is a violation, counted in `autoarima_gaps`.

Usage:
    python3 check_root_causes.py --stage diagnosis|final

Exit code 0 when every count is zero; 1 otherwise. Always prints exactly one
summary line:
    root-cause check: rows=R invalid=I uncovered=U missing_drafts=D \
repro_mismatch=M orphans=O uncaused=C autoarima_gaps=A
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
FIXTURE_DIR = REPO_ROOT / "validation" / "reference" / "fixtures" / "r_forecast"
AIR_FIXTURE = FIXTURE_DIR / "air_passengers.json"
SIM_FIXTURE = FIXTURE_DIR / "simulated_series.json"
UPSTREAM_DIR = Path(__file__).resolve().parent / "upstream-issues"
DIAGNOSIS_TEST = (
    REPO_ROOT
    / "crates"
    / "anofox-fcst-core"
    / "tests"
    / "r_parity_diagnosis.rs"
)

# The 11 Task 1 pre-diagnosis check IDs (trend/holt/Holt is added by Task 2,
# making 12 — accepted at --stage diagnosis without requiring it be present
# before Task 2 runs; see "stage diagnosis" coverage rule below).
TASK1_CHECK_IDS = [
    "air/holt/ETS:AAN",
    "air/holt/Holt",
    "air/hw_additive/HoltWinters",
    "air/hw_additive/ETS:AAA",
    "air/hw_multiplicative/ETS:MAM",
    "air/ets_auto/AutoETS",
    "air/ets_MAM/ETS:MAM",
    "air/ets_AAA/ETS:AAA",
    "rw/holt/ETS:AAN",
    "rw/holt/Holt",
    "inter/ses/SESOptimized",
]
TASK2_ADDED_CHECK_ID = "trend/holt/Holt"

VALID_LOCATIONS = {"extension", "crate", "reference"}


def is_resolved_row(row: dict) -> bool:
    """A root_causes row is resolved once it no longer describes an active
    gap — either an extension-side fix (`resolution` starts with `fixed:`)
    or a crate-side issue closed by an upstream release (`resolution`
    starts with `resolved:`, e.g. RC-05 / anofox-forecast 0.16.2). Resolved
    rows are historical record and are exempt from `autoarima_gaps`."""
    resolution = row.get("resolution", "") or ""
    return resolution.startswith("fixed:") or resolution.startswith("resolved:")


def load(path: Path) -> dict:
    with path.open() as fh:
        return json.load(fh)


def is_autoarima_check(check_id: str) -> bool:
    return check_id.endswith("/AutoARIMA")


def rust_lines(path: Path) -> list[str]:
    if not path.exists():
        return []
    return [line.strip() for line in path.read_text().splitlines()]


def draft_rust_block(draft_text: str) -> list[str]:
    """Extract the single ```rust ... ``` fenced block's non-blank lines,
    excluding the `fn main() {` line and the lines that are purely a
    dependency comment / closing brace of fn main (those have no reusable
    semantic content and are not required to appear verbatim elsewhere)."""
    m = re.search(r"```rust\n(.*?)```", draft_text, re.DOTALL)
    if not m:
        return []
    lines = []
    for raw in m.group(1).splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("// Cargo.toml:"):
            continue
        if line == "fn main() {":
            continue
        if line == "}":
            continue
        lines.append(line)
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", choices=["diagnosis", "final"], required=True)
    args = parser.parse_args()

    invalid = 0
    uncovered = 0
    missing_drafts = 0
    repro_mismatch = 0
    orphans = 0
    uncaused = 0
    autoarima_gaps = 0

    air = load(AIR_FIXTURE)
    sim = load(SIM_FIXTURE)

    air_rc = air.get("tolerance", {}).get("root_causes")
    sim_rc = sim.get("tolerance", {}).get("root_causes")

    if air_rc is None or sim_rc is None:
        print(
            "root-cause check: rows=0 invalid=1 uncovered=1 missing_drafts=0 "
            "repro_mismatch=0 orphans=0 uncaused=0 autoarima_gaps=0"
        )
        return 1

    if air_rc != sim_rc:
        invalid += 1
    root_causes = air_rc

    seen_ids: set[str] = set()
    all_checks_in_rows: set[str] = set()
    rc_lines = rust_lines(DIAGNOSIS_TEST)

    for row in root_causes:
        rid = row.get("id", "")
        checks = row.get("checks", [])
        location = row.get("location", "")
        cause = row.get("cause", "")
        evidence = row.get("evidence", "")
        resolution = row.get("resolution", "")
        upstream = row.get("upstream", "")

        if not re.fullmatch(r"RC-\d\d", rid) or rid in seen_ids:
            invalid += 1
        seen_ids.add(rid)

        if not checks or not cause or not evidence or not resolution:
            invalid += 1
        if location not in VALID_LOCATIONS:
            invalid += 1

        resolved = is_resolved_row(row)

        for check_id in checks:
            all_checks_in_rows.add(check_id)
            if is_autoarima_check(check_id) and location in ("crate", "reference"):
                if not resolved:
                    autoarima_gaps += 1

        if location == "extension":
            if not resolution.startswith("fixed:"):
                invalid += 1
        elif location == "crate":
            has_tracked = upstream.startswith("tracked:")
            has_fixed = upstream.startswith("fixed:")
            draft_match = re.match(r"draft:\s*(\S+)", upstream)
            draft_path = None
            if draft_match:
                draft_path = UPSTREAM_DIR / draft_match.group(1)
            if not has_tracked and not has_fixed and not (draft_path and draft_path.exists()):
                invalid += 1
                missing_drafts += 1
            elif draft_path and draft_path.exists():
                draft_rust = draft_rust_block(draft_path.read_text())
                for line in draft_rust:
                    if line not in rc_lines:
                        repro_mismatch += 1
        elif location == "reference":
            if upstream:
                invalid += 1

    # stage diagnosis: every pre-diagnosis Task 1 check ID (and, once
    # Task 2 has run, trend/holt/Holt) must appear in some row's checks.
    required_ids = list(TASK1_CHECK_IDS)
    if TASK2_ADDED_CHECK_ID in all_checks_in_rows:
        required_ids.append(TASK2_ADDED_CHECK_ID)
    for check_id in required_ids:
        if check_id not in all_checks_in_rows:
            uncovered += 1

    if args.stage == "final":
        air_pc = air.get("tolerance", {}).get("parity_checks", [])
        sim_pc = sim.get("tolerance", {}).get("parity_checks", [])
        all_checks = air_pc + sim_pc
        all_check_ids = set()
        rc_by_id = {row["id"]: row for row in root_causes if "id" in row}

        for check in all_checks:
            check_id = f"{check.get('series')}/{check.get('r_key')}/{check.get('surface')}"
            all_check_ids.add(check_id)

            cause_field = check.get("cause", "") or ""
            cited_ids = [c.strip() for c in cause_field.split(",") if c.strip()]

            if check.get("status") == "known_gap":
                if is_autoarima_check(check.get("surface", "")):
                    # D-16's one allowed AutoARIMA gap (air/auto_arima/AutoARIMA,
                    # RC-05) was retired on 18-05: no AutoARIMA check may be
                    # known_gap anymore.
                    autoarima_gaps += 1
                if not cited_ids:
                    uncaused += 1
                    continue
                missing_id = any(cid not in rc_by_id for cid in cited_ids)
                has_crate_or_ref = any(
                    cid in rc_by_id and rc_by_id[cid]["location"] in ("crate", "reference")
                    for cid in cited_ids
                )
                if missing_id or not has_crate_or_ref:
                    uncaused += 1

        # every check ID listed in a root_causes row must exist in parity_checks
        for check_id in all_checks_in_rows:
            if check_id not in all_check_ids:
                orphans += 1

    print(
        f"root-cause check: rows={len(root_causes)} invalid={invalid} "
        f"uncovered={uncovered} missing_drafts={missing_drafts} "
        f"repro_mismatch={repro_mismatch} orphans={orphans} uncaused={uncaused} "
        f"autoarima_gaps={autoarima_gaps}"
    )

    total = invalid + uncovered + missing_drafts + repro_mismatch + orphans + uncaused + autoarima_gaps
    return 1 if total > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
