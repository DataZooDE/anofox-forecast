#!/usr/bin/env bash
# Set the Claude Code plugin's versions to the newest release in CHANGELOG.md
# and the anofox-forecast crate version in Cargo.lock.
#
# Part of every release, right after the "## [YYYY.MM.DD]" heading is written:
#   ./scripts/bump_plugin_version.sh        (or: make bump-plugin)
# It rewrites plugins/anofox-forecast/.claude-plugin/plugin.json "version",
# every SKILL.md "version:" line and every SKILL.md header
#   **Extension:** `anofox_forecast` vX (Rust crate `anofox-forecast` vY)
# Prose that names a version ("since v2026.10.16") is left alone.
# `make check-docs` fails while any of them is behind, and each deploy waits
# for the CI run of that check, so a forgotten bump stops the release instead
# of shipping stale skills (they sat at 2026.09.26 for three releases).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
plugin="$root/plugins/anofox-forecast"

version="$(sed -nE 's/^## \[([0-9]{4}\.[0-9]{1,2}\.[0-9]{1,2})\].*/\1/p' "$root/CHANGELOG.md" | head -1)"
if [[ -z "$version" ]]; then
  echo "no '## [YYYY.MM.DD]' release heading in CHANGELOG.md" >&2
  exit 1
fi
crate="$(grep -A1 '^name = "anofox-forecast"$' "$root/Cargo.lock" | sed -nE 's/^version = "([^"]+)"/\1/p' | head -1)"
if [[ -z "$crate" ]]; then
  echo "no anofox-forecast package in Cargo.lock" >&2
  exit 1
fi

sed -i -E 's/^(  "version": ")[^"]*(",?)$/\1'"$version"'\2/' "$plugin/.claude-plugin/plugin.json"
for f in "$plugin"/skills/*/SKILL.md; do
  sed -i -E \
    -e 's/^version: .*/version: '"$version"'/' \
    -e 's/^(\*\*Extension:\*\* `anofox_forecast` v)[0-9.]+/\1'"$version"'/' \
    -e 's/^(\*\*Extension:\*\* .*\(Rust crate `anofox-forecast` v)[0-9.]+/\1'"$crate"'/' \
    "$f"
done

echo "plugin and skills set to $version (crate $crate)"
grep -H '"version"' "$plugin/.claude-plugin/plugin.json"
grep -H -E '^version: ' "$plugin"/skills/*/SKILL.md
