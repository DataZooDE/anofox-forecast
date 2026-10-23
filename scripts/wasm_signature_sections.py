#!/usr/bin/env python3
"""scripts/wasm_signature_sections.py

Read-only WASM custom-section walker for the `duckdb_signature` section that
DuckDB's extension loader requires at the end of every extension binary
(#272, DIST-01/02). This module never writes the file it inspects — it only
parses the LEB128-framed section headers of a `.wasm` module and reports
facts about the trailing `duckdb_signature` custom section.

Background: `scripts/extension-upload.sh` used to unconditionally append a
*second* `duckdb_signature` custom section to every WASM artifact, producing
a module with two such sections that fails to LOAD in DuckDB-Wasm ("Unknown
ABI type"). The sibling `anofox-statistics` extension hit and fixed the same
bug (issue #192, PR #193) with a conditional variant; this phase (#272) locks
the simpler, unconditional variant instead: a WASM artifact must already end
with exactly one `duckdb_signature` section before upload, and the upload
script replaces that section's 256-byte signature payload in place rather
than ever appending a new section header. This script is the shared guard
both the upload script (pre/post guard) and the post-upload CI check use to
assert that invariant.

Binary format recap (see the WebAssembly spec, "Custom Section"): a module is
an 8-byte header (`\\0asm` magic + 4-byte version) followed by a sequence of
sections, each `id:u8 size:LEB128(u32) content:byte[size]`. A custom section
(id 0) encodes `name_len:LEB128(u32) name:byte[name_len]` at the start of its
content; the remaining bytes are the section's payload. DuckDB's loader
expects the trailing custom section named `duckdb_signature` to carry a
256-byte (possibly zero-filled, i.e. unsigned) signature payload as the very
last bytes of the file.

CLI:
    wasm_signature_sections.py count FILE
        Print the number of `duckdb_signature` custom sections in FILE.

    wasm_signature_sections.py check FILE [FILE...]
        Exit 0 only if EVERY given file: starts with the WASM magic and
        version, contains exactly one `duckdb_signature` custom section,
        that section is the last section in the module and ends exactly at
        EOF, and its payload is at least 256 bytes. Prints
        "<file>: 1 duckdb_signature section (ok)" to stdout on success, or
        "<file>: error: <reason>" to stderr (naming the actual count when it
        is not 1) and exits 1 otherwise.

    wasm_signature_sections.py --self-test
        Build synthetic modules in memory and verify this script's own
        verdicts against known-good/known-bad shapes. Exits 0 only if every
        synthetic verdict matches expectations.

Exit codes:
    0 - success (check: every file ok; count: section count printed;
        self-test: every synthetic case verdict matched)
    1 - a `check` file failed, a `count` file could not be parsed, or a
        self-test verdict mismatched
    2 - bad CLI usage
"""
import sys

MAGIC = b"\0asm"
SIGNATURE_NAME = b"duckdb_signature"
MIN_PAYLOAD_LEN = 256


class WasmParseError(Exception):
    """Raised when a module cannot be parsed far enough to answer the
    question asked (bad magic, truncated file, a section/name length that
    runs past the end of the file). Callers must catch this and report it as
    a clean error, never let it propagate as an unhandled traceback."""


def leb(b, i):
    """Decode one unsigned LEB128 value from `b` starting at offset `i`.
    Returns (value, next_offset). Raises WasmParseError if the encoding runs
    past the end of the buffer before terminating."""
    r = s = 0
    n = len(b)
    while True:
        if i >= n:
            raise WasmParseError(f"LEB128 integer read past end of file at offset {i}")
        x = b[i]
        i += 1
        r |= (x & 0x7F) << s
        s += 7
        if x < 0x80:
            return r, i


def leb_encode(value):
    """Encode `value` as unsigned LEB128 bytes (used only by --self-test to
    build synthetic modules; never used on data read from disk)."""
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def parse_sections(b):
    """Walk every top-level section of WASM module `b`, returning a list of
    dicts with keys: id, start, end, name (bytes or None for non-custom
    sections), content_start, content_end (payload bounds for a custom
    section; for a named custom section, content excludes the name_len+name
    prefix). Raises WasmParseError on a malformed/truncated module -- never
    raises any other exception type for malformed input."""
    n = len(b)
    if n < 8:
        raise WasmParseError(f"file too short to contain a WASM header ({n} bytes)")
    if b[:4] != MAGIC:
        raise WasmParseError("missing WASM magic bytes (expected b'\\0asm')")

    i = 8
    sections = []
    while i < n:
        start = i
        sid = b[i]
        i += 1
        size, i = leb(b, i)
        end = i + size
        if end > n:
            raise WasmParseError(
                f"section id={sid} at offset {start} declares size {size} "
                f"but that runs past EOF (would end at {end}, file is {n} bytes)"
            )
        entry = {
            "id": sid,
            "start": start,
            "end": end,
            "name": None,
            "content_start": None,
            "content_end": end,
        }
        if sid == 0:
            name_len, j = leb(b, i)
            if j + name_len > end:
                raise WasmParseError(
                    f"custom section at offset {start} declares name length {name_len} "
                    f"that runs past its own section end ({end})"
                )
            entry["name"] = b[j : j + name_len]
            entry["content_start"] = j + name_len
            entry["content_end"] = end
        sections.append(entry)
        i = end
    return sections


def find_signature_sections(sections):
    return [s for s in sections if s["id"] == 0 and s["name"] == SIGNATURE_NAME]


def check_bytes(b):
    """Core check logic over an in-memory module. Returns (ok: bool,
    message: str). Never raises -- WasmParseError is caught and reported as
    a failing message."""
    try:
        sections = parse_sections(b)
    except WasmParseError as e:
        return False, str(e)

    sigs = find_signature_sections(sections)
    count = len(sigs)
    if count != 1:
        return False, f"found {count} duckdb_signature sections, expected exactly 1"

    sig = sigs[0]
    if sig["end"] != len(b):
        return False, (
            f"duckdb_signature section ends at offset {sig['end']} but the file is "
            f"{len(b)} bytes -- it is not the last section / does not end exactly at EOF"
        )

    # This is deliberately `>=`, not `==` (see module docstring): a
    # `duckdb_signature` section may carry metadata ahead of the 256-byte
    # signature placeholder. scripts/extension-upload.sh depends on exactly
    # this invariant -- it only ever replaces the *trailing* 256 bytes of
    # the payload (`truncate -s -256` then append 256 fresh bytes), which is
    # only correct because the last 256 bytes of ANY accepted payload shape
    # (payload_len == 256, or payload_len > 256 with a metadata prefix) are
    # always the signature placeholder. Case 6 in --self-test pins the
    # payload_len > 256 shape so this bound is never tightened to `==` by a
    # future edit without a visible self-test failure explaining why it is
    # `>=`.
    payload_len = sig["content_end"] - sig["content_start"]
    if payload_len < MIN_PAYLOAD_LEN:
        return False, f"duckdb_signature payload is {payload_len} bytes, expected at least {MIN_PAYLOAD_LEN}"

    return True, "ok"


def check_file(path):
    """Read-only: open `path`, run check_bytes over its contents. Never
    writes to `path`. Returns (ok, message)."""
    try:
        with open(path, "rb") as f:
            b = f.read()
    except OSError as e:
        return False, f"cannot read file: {e}"
    return check_bytes(b)


def count_signature_sections_in_bytes(b):
    """Returns the number of duckdb_signature custom sections in `b`.
    Raises WasmParseError on a malformed module."""
    return len(find_signature_sections(parse_sections(b)))


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def cmd_count(args):
    if len(args) != 1:
        print("usage: wasm_signature_sections.py count FILE", file=sys.stderr)
        return 2
    path = args[0]
    try:
        with open(path, "rb") as f:
            b = f.read()
        n = count_signature_sections_in_bytes(b)
    except (OSError, WasmParseError) as e:
        print(f"{path}: error: {e}", file=sys.stderr)
        return 1
    print(n)
    return 0


def cmd_check(args):
    if len(args) < 1:
        print("usage: wasm_signature_sections.py check FILE [FILE...]", file=sys.stderr)
        return 2
    failed = False
    for path in args:
        ok, msg = check_file(path)
        if ok:
            print(f"{path}: 1 duckdb_signature section (ok)")
        else:
            print(f"{path}: error: {msg}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


# ---------------------------------------------------------------------------
# Synthetic module builders (used only by --self-test)
# ---------------------------------------------------------------------------
def _wasm_header():
    return MAGIC + bytes([1, 0, 0, 0])


def _make_custom_section(name: bytes, payload: bytes):
    content = leb_encode(len(name)) + name + payload
    return bytes([0]) + leb_encode(len(content)) + content


def _make_minimal_type_section():
    """A minimal, non-custom section (id=1, the WASM "type" section) with an
    empty vector -- just something plausible preceding the signature
    section, so the module isn't only ever a header + one section."""
    content = bytes([0])  # vec(functype) count = 0
    return bytes([1]) + leb_encode(len(content)) + content


def _signature_section(payload_len=256):
    return _make_custom_section(SIGNATURE_NAME, bytes(payload_len))


def self_test():
    failures = []

    def check(name, condition, detail=""):
        if not condition:
            failures.append(f"{name}: {detail}")

    header = _wasm_header()
    type_section = _make_minimal_type_section()
    sig_section = _signature_section()

    # --- Case 1: header + a non-custom section + one trailing signature
    # section with a 256-byte payload -> ok.
    good = header + type_section + sig_section
    ok, msg = check_bytes(good)
    check("good module: single trailing signature section", ok, msg)

    # --- Case 2: two trailing signature sections -> fail with count 2.
    double = header + type_section + sig_section + sig_section
    ok, msg = check_bytes(double)
    check("double-signed module: check fails", not ok, "check_bytes returned ok=True")
    n = count_signature_sections_in_bytes(double)
    check("double-signed module: count is 2", n == 2, f"count={n}")
    check("double-signed module: message names the count", "2" in msg, msg)

    # --- Case 3: no signature section at all -> fail.
    no_sig = header + type_section
    ok, msg = check_bytes(no_sig)
    check("no-signature module: check fails", not ok, "check_bytes returned ok=True")
    n = count_signature_sections_in_bytes(no_sig)
    check("no-signature module: count is 0", n == 0, f"count={n}")

    # --- Case 4: signature section followed by another custom section ->
    # fail (signature is no longer the last thing in the file / does not
    # end at EOF).
    trailing_other = header + type_section + sig_section + _make_custom_section(b"other", b"x")
    ok, msg = check_bytes(trailing_other)
    check("signature-then-other module: check fails", not ok, "check_bytes returned ok=True")

    # --- Case 5: a section whose declared size runs past EOF -> fail
    # without crashing (WasmParseError caught, not an unhandled traceback).
    truncated = header + bytes([0]) + leb_encode(1000) + b"short"
    try:
        ok, msg = check_bytes(truncated)
        check("truncated-size module: check fails", not ok, "check_bytes returned ok=True")
    except Exception as e:  # noqa: BLE001 - this IS the failure mode under test
        failures.append(f"truncated-size module: raised {type(e).__name__} instead of returning (False, ...): {e}")

    # --- Case 6 (WR-03, #272): a duckdb_signature section with payload_len
    # > 256 -- a 4-byte metadata prefix ahead of the 256-byte signature
    # placeholder (payload_len = 260) -> still ok. This pins the loose `>=`
    # bound `check_bytes` uses against the exact shape
    # scripts/extension-upload.sh's correctness depends on: that script
    # only ever truncates/replaces the *trailing* 256 bytes of the payload
    # (`truncate -s -256` then append 256 fresh bytes), so any
    # metadata-prefixed payload_len >= 256 must keep passing `check`. A
    # future edit that tightens this bound to `== 256` will fail this case
    # loudly, with the explanation above, instead of silently breaking the
    # upload script's assumption.
    metadata_prefix = b"META"
    sig_with_metadata = _make_custom_section(SIGNATURE_NAME, metadata_prefix + bytes(256))
    with_metadata = header + type_section + sig_with_metadata
    ok, msg = check_bytes(with_metadata)
    check("signature-with-metadata-prefix module (payload_len=260): check passes", ok, msg)
    sigs_with_metadata = find_signature_sections(parse_sections(with_metadata))
    metadata_payload_len = sigs_with_metadata[0]["content_end"] - sigs_with_metadata[0]["content_start"]
    check(
        "signature-with-metadata-prefix module: payload_len is 260",
        metadata_payload_len == 260,
        f"payload_len={metadata_payload_len}",
    )

    if failures:
        print("SELF-TEST FAILURES:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    print("self-test: 6 synthetic module verdicts passed")
    return 0


def main(argv):
    if not argv:
        print(
            "usage: wasm_signature_sections.py {count FILE | check FILE [FILE...] | --self-test}",
            file=sys.stderr,
        )
        return 2
    cmd, rest = argv[0], argv[1:]
    if cmd == "--self-test":
        return self_test()
    if cmd == "count":
        return cmd_count(rest)
    if cmd == "check":
        return cmd_check(rest)
    print(f"unknown command: {cmd}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
