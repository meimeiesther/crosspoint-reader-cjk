#!/usr/bin/env python3
"""Overflow glyph lookup must handle wrapped/evicted ring slots."""
from pathlib import Path
import sys

source = Path("lib/EpdFont/SdCardFont.cpp").read_text()


def body(start_marker: str, end_marker: str) -> str:
    start = source.find(start_marker)
    return source[start : source.find(end_marker, start)]


failures = []
pointer_lookup = body("bool SdCardFont::isOverflowGlyph", "SdCardFont* SdCardFont::fromMissCtx")
if pointer_lookup.count("i < overflowCapacity_") < 2:
    failures.append("overflow glyph/bitmap lookup still assumes contiguous occupied slots")

# After budget evictions the live entries start at overflowOldest_, so the
# codepoint lookup and the teardown must walk the whole ring, not [0, count).
miss = body("const EpdGlyph* SdCardFont::onGlyphMiss", "bool SdCardFont::isOverflowGlyph")
if "(self->overflowOldest_ + k) % self->overflowCapacity_" not in miss:
    failures.append("overflow codepoint lookup does not start at the ring's oldest entry")
clear = body("void SdCardFont::clearOverflow()", "\n}\n")
if "i < overflowCapacity_" not in clear or "i < overflowCount_" in clear:
    failures.append("clearOverflow can leak bitmaps of wrapped ring entries")

if failures:
    print("\n".join(failures), file=sys.stderr)
    raise SystemExit(1)
print("SD_FONT_OVERFLOW_WRAPPED_LOOKUP_OK")
