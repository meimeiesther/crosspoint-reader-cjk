#!/usr/bin/env python3
"""SD font page bitmaps must not need one large contiguous allocation.

A dense CJK page at reader sizes needs 30KB+ of glyph bitmaps. Once the heap
fragments, a block that size is rarely free, and page prewarm used to fall back
to one SD read per glyph (~2s per page). Pages are now packed into fixed-size
chunks (see ChunkedBitmapLayout.h); this checks the SdCardFont/renderer wiring.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "lib" / "EpdFont" / "SdCardFont.h").read_text(encoding="utf-8")
source = (ROOT / "lib" / "EpdFont" / "SdCardFont.cpp").read_text(encoding="utf-8")
renderer = (ROOT / "lib" / "GfxRenderer" / "GfxRenderer.cpp").read_text(encoding="utf-8")


def body(text: str, start_marker: str, end_marker: str = "\n}\n") -> str:
    start = text.find(start_marker)
    if start < 0:
        return ""
    return text[start : text.find(end_marker, start)]


failures = []

accessor = body(renderer, "const uint8_t* GfxRenderer::getGlyphBitmap")
if "SdCardFont::glyphBitmap(fontData->glyphMissCtx, glyph)" not in accessor:
    failures.append("renderer does not resolve SD glyph bitmaps through SdCardFont::glyphBitmap")

prewarm = body(source, "int SdCardFont::prewarmStyle")
if "ensureArrayCapacity(s.miniBitmap" in prewarm or "new (std::nothrow) uint8_t[totalBitmapSize]" in prewarm:
    failures.append("page prewarm still allocates one contiguous bitmap block")
if "ensureMiniChunks(s, chunkBytes, chunkCount)" not in prewarm:
    failures.append("page prewarm does not allocate bitmap chunks")
if "std::max(MINI_CHUNK_BYTES, maxGlyphBytes)" not in prewarm:
    failures.append("chunk size can be smaller than the page's largest glyph")
if prewarm.count("ChunkedBitmapLayout::place(") < 2:
    failures.append("chunk count and read loop do not share the same packing")

lookup = body(source, "const uint8_t* SdCardFont::glyphBitmap")
for needle, message in (
    ("isOverflowGlyph(glyph)", "glyphBitmap ignores overflow glyphs"),
    ("s.miniMetadataOnly", "glyphBitmap resolves file offsets of a metadata-only build"),
    (">= s.miniChunkCount", "glyphBitmap does not bound the chunk index"),
    ("ChunkedBitmapLayout::resolve(", "glyphBitmap does not use the shared chunk addressing"),
):
    if needle not in lookup:
        failures.append(message)

free_mini = body(source, "void SdCardFont::freeStyleMiniData")
if "freeMiniChunks(s);" not in free_mini or "delete[] s.miniBitmap" in free_mini:
    failures.append("freeStyleMiniData does not release chunks (or frees the non-owned arena)")
free_chunks = body(source, "void SdCardFont::freeMiniChunks")
if "delete[] s.miniChunks[i]" not in free_chunks or "delete[] s.miniChunks;" not in free_chunks:
    failures.append("freeMiniChunks leaks chunks or the pointer array")
ensure = body(source, "bool SdCardFont::ensureMiniChunks")
if "std::copy(s.miniChunks" not in ensure:
    failures.append("growing the chunk pointer array drops existing chunks")
if "if (s.miniChunkBytes != chunkBytes) freeMiniChunks(s);" not in ensure:
    failures.append("chunks of a different size are reused")
if "uint8_t** miniChunks = nullptr;" not in header:
    failures.append("PerStyle has no page chunk list")

if failures:
    print("\n".join(failures), file=sys.stderr)
    raise SystemExit(1)
print("SD_FONT_CHUNKED_PAGE_BITMAP_OK")
