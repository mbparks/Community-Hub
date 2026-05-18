#!/usr/bin/env python3
"""
build_gz_pages.py

Generates Community_Hub_pages_gz.h from Community_Hub_pages.h.

Reads each `const char NAME[] PROGMEM = R"PAGE(...)PAGE";` block, gzips the
inner content, and emits a companion header containing:

    const uint8_t NAME_GZ[]     PROGMEM = { 0x1f, 0x8b, ... };
    const size_t  NAME_GZ_LEN            = N;

The firmware serves these blobs directly with Content-Encoding: gzip, which
typically cuts payload by 65-75% on HTML/CSS/JS.

Re-run this script any time you edit Community_Hub_pages.h.

Usage:
    python3 tools/build_gz_pages.py [pages.h] [output.h]

Defaults:
    pages.h   = Community_Hub_pages.h    (in current directory)
    output.h  = Community_Hub_pages_gz.h (in current directory)
"""
import gzip
import re
import sys
from pathlib import Path


# Match: const char NAME[] PROGMEM = R"PAGE(...content...)PAGE";
# DOTALL so the inner block can span many lines. Non-greedy on content.
PAGE_PATTERN = re.compile(
    r'const\s+char\s+(\w+)\s*\[\s*\]\s+PROGMEM\s*=\s*R"PAGE\((.*?)\)PAGE"\s*;',
    re.DOTALL,
)


def gzip_bytes(data: bytes) -> bytes:
    """Gzip with max compression and no embedded timestamp (so output is stable)."""
    return gzip.compress(data, compresslevel=9, mtime=0)


def emit_byte_array(name: str, blob: bytes, per_line: int = 16) -> str:
    """Format a uint8_t PROGMEM array. 16 bytes per line, hex literals."""
    lines = []
    for i in range(0, len(blob), per_line):
        chunk = blob[i : i + per_line]
        hex_bytes = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append("  " + hex_bytes + ("," if i + per_line < len(blob) else ""))
    body = "\n".join(lines)
    return (
        f"const uint8_t {name}_GZ[] PROGMEM = {{\n{body}\n}};\n"
        f"const size_t  {name}_GZ_LEN = {len(blob)};\n"
    )


def main(src_path: Path, dst_path: Path) -> int:
    if not src_path.exists():
        print(f"error: source file not found: {src_path}", file=sys.stderr)
        return 1

    source = src_path.read_text(encoding="utf-8")
    matches = list(PAGE_PATTERN.finditer(source))
    if not matches:
        print(f"error: no R\"PAGE(...)PAGE\" blocks found in {src_path}", file=sys.stderr)
        return 1

    header_lines = [
        "// ============================================================================",
        f"// {dst_path.name}",
        "//",
        f"// Generated from {src_path.name} by tools/build_gz_pages.py.",
        "// DO NOT EDIT BY HAND. Edit the source pages.h and re-run the script.",
        "//",
        "// Each constant is a gzip-compressed byte array. The firmware serves these",
        "// directly with Content-Encoding: gzip; modern browsers decompress",
        "// transparently. This typically cuts HTML payload by 65-75 percent.",
        "// ============================================================================",
        "",
        "#pragma once",
        "#include <Arduino.h>",
        "#include <pgmspace.h>",
        "#include <stddef.h>",
        "",
    ]

    summary = []
    for m in matches:
        name = m.group(1)
        raw = m.group(2).encode("utf-8")
        gz = gzip_bytes(raw)
        ratio = (1.0 - len(gz) / len(raw)) * 100.0 if raw else 0.0
        summary.append((name, len(raw), len(gz), ratio))

        header_lines.append("// ----------------------------------------------------------------------------")
        header_lines.append(f"// {name}: {len(raw)} bytes raw -> {len(gz)} bytes gzipped ({ratio:.1f}% smaller)")
        header_lines.append("// ----------------------------------------------------------------------------")
        header_lines.append(emit_byte_array(name, gz))
        header_lines.append("")

    dst_path.write_text("\n".join(header_lines), encoding="utf-8")

    # Print a build-time summary so re-runs feel informative.
    total_raw = sum(s[1] for s in summary)
    total_gz = sum(s[2] for s in summary)
    overall = (1.0 - total_gz / total_raw) * 100.0 if total_raw else 0.0
    print(f"Wrote {dst_path} ({len(matches)} blob(s)):")
    for name, raw, gz, ratio in summary:
        print(f"  {name:<20} {raw:>7,} -> {gz:>6,} bytes  ({ratio:5.1f}% smaller)")
    print(f"  {'TOTAL':<20} {total_raw:>7,} -> {total_gz:>6,} bytes  ({overall:5.1f}% smaller)")
    return 0


if __name__ == "__main__":
    args = sys.argv[1:]
    src = Path(args[0]) if len(args) >= 1 else Path("Community_Hub_pages.h")
    dst = Path(args[1]) if len(args) >= 2 else Path("Community_Hub_pages_gz.h")
    sys.exit(main(src, dst))
