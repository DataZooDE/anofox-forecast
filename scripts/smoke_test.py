#!/usr/bin/env python3
"""Post-build smoke test: does the shipped artifact actually load and work?

Everything else in CI exercises ``build/release/duckdb`` -- a shell with the
extension *statically linked in*. That never loads the ``.duckdb_extension``
file users install, so it cannot catch an artifact that builds green but will
not instantiate. quack-oauth's wasm side-module is the standing example: it
"builds green but won't instantiate in the browser".

This downloads the OFFICIAL DuckDB CLI for the exact target version, installs
the artifact into it, loads it, and calls a real function -- which is what a
user does, and the only thing that proves the artifact works on this platform.

Usage:
    python3 scripts/smoke_test.py <extension_path> <duckdb_version> <arch>

    arch is the DuckDB arch name used by the build matrix, e.g. linux_amd64.

DuckDB v2.0.0 renamed its CLI release assets to ``duckdb-cli-<os>-<arch>.tar.gz``
(v1.x shipped ``duckdb_cli-<os>-<arch>.zip``). Both names are tried per arch, so
this one script keeps working across the v1.5.6 and v2.0.0 lines.
"""

from __future__ import annotations

import os
import platform
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
import zipfile

# ── Per-repo configuration ───────────────────────────────────────────────────
# The only two values that change between repositories.
EXTENSION_NAME = "anofox_forecast"

# A function that must exist and be callable with no network, no credentials
# and no external service. Loading proves the shared object resolves; it does
# not prove the registered functions work, because symbols can resolve lazily
# and only fail when actually called. Verified against a real build of this
# extension before being committed.
SMOKE_QUERY = "SELECT count(*) AS n FROM ts_features_list();"
# ─────────────────────────────────────────────────────────────────────────────

# DuckDB v2.0.0 renamed its CLI release assets from the legacy
# `duckdb_cli-<os>-<arch>.zip` to `duckdb-cli-<os>-<arch>.tar.gz`. Each tarball
# holds a single bare `duckdb` (or `duckdb.exe`) at the archive root. Per arch
# we list the >= 2.0 tarball name first and the legacy v1.x zip name second, so
# one script serves the v1.5.6 and v2.0.0 (and later) lines -- whichever
# asset the requested `duckdb_version` actually published.
ARCH_TO_CLI_ASSETS: dict[str, tuple[str, ...]] = {
    "linux_amd64": ("duckdb-cli-linux-amd64.tar.gz", "duckdb_cli-linux-amd64.zip"),
    "linux_arm64": ("duckdb-cli-linux-arm64.tar.gz", "duckdb_cli-linux-aarch64.zip"),
    "linux_amd64_musl": (
        "duckdb-cli-linux-amd64.tar.gz",
        "duckdb_cli-linux-amd64.zip",
    ),
    "osx_amd64": ("duckdb-cli-osx-universal.tar.gz", "duckdb_cli-osx-universal.zip"),
    "osx_arm64": ("duckdb-cli-osx-universal.tar.gz", "duckdb_cli-osx-universal.zip"),
    "windows_amd64": (
        "duckdb-cli-windows-amd64.tar.gz",
        "duckdb_cli-windows-amd64.zip",
    ),
}


def _safe_extract_tar(tar_path: str, dest_dir: str) -> None:
    with tarfile.open(tar_path, "r:gz") as tf:
        if hasattr(tarfile, "data_filter"):
            tf.extractall(dest_dir, filter="data")
            return
        # Python < 3.12 has no extraction filters: reject path traversal
        # ourselves before extracting (T-261022-03).
        for member in tf.getmembers():
            name = member.name
            if os.path.isabs(name) or ".." in name.split("/"):
                raise SystemExit(
                    f"Refusing to extract unsafe tar member: {name!r}"
                )
        tf.extractall(dest_dir)


def _download_duckdb_cli(version: str, arch: str, dest_dir: str) -> str:
    asset_names = ARCH_TO_CLI_ASSETS.get(arch)
    if asset_names is None:
        raise SystemExit(
            f"Unsupported arch '{arch}'. Supported: {sorted(ARCH_TO_CLI_ASSETS)}"
        )

    tried_urls: list[str] = []
    archive_path: str | None = None
    asset_name: str | None = None
    for candidate in asset_names:
        url = f"https://github.com/duckdb/duckdb/releases/download/{version}/{candidate}"
        tried_urls.append(url)
        dest = os.path.join(dest_dir, candidate)
        print(f"Downloading official DuckDB {version} CLI ({arch}):\n  {url}")
        try:
            urllib.request.urlretrieve(url, dest)
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                print(f"  not found ({candidate}), trying next asset name")
                continue
            raise
        archive_path = dest
        asset_name = candidate
        break

    if archive_path is None or asset_name is None:
        raise SystemExit(
            f"Could not find a DuckDB {version} CLI asset for arch '{arch}'. "
            f"Tried:\n  " + "\n  ".join(tried_urls)
        )

    if asset_name.endswith(".tar.gz"):
        _safe_extract_tar(archive_path, dest_dir)
        members: list[str]
        with tarfile.open(archive_path, "r:gz") as tf:
            members = tf.getnames()
    else:
        with zipfile.ZipFile(archive_path, "r") as zf:
            zf.extractall(dest_dir)
        members = zipfile.ZipFile(archive_path).namelist()

    bin_name = "duckdb.exe" if platform.system() == "Windows" else "duckdb"
    binary = os.path.join(dest_dir, bin_name)
    if not os.path.isfile(binary):
        raise SystemExit(
            f"DuckDB binary not found after extraction: {binary}\n"
            f"Archive ({asset_name}) contents: {members}"
        )

    if platform.system() != "Windows":
        os.chmod(binary, 0o755)
    return binary


def _run_sql(duckdb_bin: str, sql: str, home: str) -> subprocess.CompletedProcess:
    # An isolated HOME keeps the developer's real ~/.duckdb untouched, and
    # -unsigned is required because a locally built artifact is not signed.
    env = dict(os.environ)
    env["HOME"] = home
    env["USERPROFILE"] = home
    return subprocess.run(
        [duckdb_bin, "-unsigned", "-noheader", "-list", "-c", sql],
        capture_output=True,
        text=True,
        env=env,
        timeout=300,
    )


def _fail(message: str, proc: subprocess.CompletedProcess | None = None) -> None:
    out = ""
    if proc is not None:
        out = f"\n  exit code : {proc.returncode}\n"
        if proc.stdout:
            out += f"  stdout    : {proc.stdout.strip()}\n"
        if proc.stderr:
            out += f"  stderr    : {proc.stderr.strip()}\n"
    raise SystemExit(f"Smoke test FAILED: {message}{out}")


def run_smoke_test(extension_path: str, duckdb_version: str, arch: str) -> None:
    if not os.path.isfile(extension_path):
        raise SystemExit(f"Smoke test FAILED: artifact not found: {extension_path}")

    # Forward slashes work in DuckDB SQL on every platform, Windows included.
    ext = extension_path.replace("\\", "/")

    with tempfile.TemporaryDirectory() as tmpdir:
        duckdb_bin = _download_duckdb_cli(duckdb_version, arch, tmpdir)
        home = os.path.join(tmpdir, "home")
        os.makedirs(home, exist_ok=True)

        print(
            f"\nSmoke test\n"
            f"  extension : {extension_path}\n"
            f"  duckdb    : {duckdb_version} ({arch})\n"
        )

        # 1. Install -- catches a truncated or wrong-platform artifact.
        print("[1/3] Installing the artifact into a stock CLI")
        proc = _run_sql(duckdb_bin, f"INSTALL '{ext}';", home)
        if proc.returncode != 0:
            _fail("the artifact is not installable on this platform", proc)

        # 2. Load, and confirm DuckDB itself agrees it loaded.
        print("[2/3] Loading and checking duckdb_extensions()")
        proc = _run_sql(
            duckdb_bin,
            f"LOAD {EXTENSION_NAME};\n"
            f"SELECT loaded FROM duckdb_extensions() "
            f"WHERE extension_name = '{EXTENSION_NAME}';",
            home,
        )
        if proc.returncode != 0:
            _fail(f"{EXTENSION_NAME} did not load", proc)
        if "true" not in (proc.stdout or "").lower():
            _fail(
                f"{EXTENSION_NAME} does not report loaded=true. An artifact that "
                f"builds green but will not instantiate is exactly what this "
                f"test exists to catch",
                proc,
            )

        # 3. Call a real function -- loading alone does not prove the registered
        #    functions work.
        print(f"[3/3] Calling a real function:\n      {SMOKE_QUERY}")
        proc = _run_sql(duckdb_bin, f"LOAD {EXTENSION_NAME};\n{SMOKE_QUERY}", home)
        if proc.returncode != 0:
            _fail("the smoke query failed", proc)
        if not (proc.stdout or "").strip():
            _fail("the smoke query returned no output", proc)
        print(f"      -> {proc.stdout.strip()}")

    print(f"\nSmoke test PASSED ({EXTENSION_NAME} on {arch})")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(
            f"Usage: {sys.argv[0]} <extension_path> <duckdb_version> <arch>"
        )
    run_smoke_test(
        extension_path=sys.argv[1],
        duckdb_version=sys.argv[2],
        arch=sys.argv[3],
    )
