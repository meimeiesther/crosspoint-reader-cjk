#!/usr/bin/env python3
"""Keep SD font heap use proportional to what a font actually needs.

Every SD font (reader plus each UI size) used to embed a 120-slot overflow
table, and paragraph layout allocated a 4096-entry codepoint buffer per
paragraph. On a fragmented heap both pushed reader page prewarm and chapter
indexing into allocation failures.
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "lib" / "EpdFont" / "SdCardFont.h").read_text(encoding="utf-8")
source = (ROOT / "lib" / "EpdFont" / "SdCardFont.cpp").read_text(encoding="utf-8")


def body(start_marker: str, end_marker: str) -> str:
    start = source.find(start_marker)
    if start < 0:
        return ""
    return source[start : source.find(end_marker, start)]


failures = []

# Overflow slot table: lazily allocated, released with the ring.
if re.search(r"OverflowEntry\s+overflow_\s*\[", header):
    failures.append("overflow slot table is embedded in every SdCardFont")
if "OverflowEntry* overflow_ = nullptr;" not in header:
    failures.append("overflow slot table is not lazily allocated")
clear = body("void SdCardFont::clearOverflow()", "\n}\n")
if "delete[] overflow_;" not in clear or "overflowCapacity_ = 0;" not in clear:
    failures.append("clearOverflow does not release the slot table")
ensure = body("bool SdCardFont::ensureOverflowTable()", "\n}\n")
if not re.search(r"OVERFLOW_TABLE_CAPACITIES\[\]\s*=\s*\{OVERFLOW_CAPACITY,\s*\d+", header):
    failures.append("slot table has no smaller low-heap capacities")
if "OVERFLOW_TABLE_CAPACITIES" not in ensure:
    failures.append("slot table allocation ignores the low-heap capacities")
# Value-initialized like the former inline `= {}` array, so unused slots never
# expose indeterminate glyph metrics.
if "new (std::nothrow) OverflowEntry[capacity]()" not in ensure:
    failures.append("slot table entries are not value-initialized")
miss = body("const EpdGlyph* SdCardFont::onGlyphMiss", "int32_t SdCardFont::overflowSlotOf")
ensure_at = miss.find("ensureOverflowTable()")
if ensure_at < 0 or ensure_at > miss.find("uint32_t slot = self->overflowNext_;"):
    failures.append("onGlyphMiss picks a slot before the table exists")
if "OVERFLOW_CAPACITY" in miss:
    failures.append("onGlyphMiss ring math ignores the allocated capacity")

# Advance-table scan: bounded by the table's remaining room.
build = body("int SdCardFont::buildAdvanceTableRange", "\n}\n")
if "MAX_UNIQUE_CODEPOINTS" in build:
    failures.append("advance scan still allocates a fixed 4096-entry buffer")
if "new (std::nothrow) uint32_t[room + 2]" not in build or "if (room == 0) return 0;" not in build:
    failures.append("advance scan buffer is not sized to the table's room")
if "advanceTableLookup(si, cp, nullptr)" not in build:
    failures.append("advance scan collects codepoints that are already cached")

if failures:
    print("\n".join(failures), file=sys.stderr)
    raise SystemExit(1)
print("SD_FONT_HEAP_FOOTPRINT_OK")
