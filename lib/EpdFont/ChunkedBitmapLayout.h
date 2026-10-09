#pragma once

#include <cstdint>

// Packs variable-size glyph bitmaps into fixed-size chunks so a page never
// needs one large contiguous allocation. Offsets are linear addresses with
// stride chunkBytes, and no bitmap straddles a chunk boundary.
namespace ChunkedBitmapLayout {

// Returns the offset for a `length`-byte bitmap and advances `cursor`,
// skipping to the next chunk when the bitmap would straddle a boundary.
// Requires length <= chunkBytes.
inline uint32_t place(uint32_t& cursor, const uint32_t length, const uint32_t chunkBytes) {
  if (cursor % chunkBytes + length > chunkBytes) cursor += chunkBytes - cursor % chunkBytes;
  const uint32_t at = cursor;
  cursor += length;
  return at;
}

inline uint32_t chunkCount(const uint32_t packedBytes, const uint32_t chunkBytes) {
  return (packedBytes + chunkBytes - 1) / chunkBytes;
}

template <typename Byte>
Byte* resolve(Byte* const* chunks, const uint32_t chunkBytes, const uint32_t offset) {
  return chunks[offset / chunkBytes] + offset % chunkBytes;
}

}  // namespace ChunkedBitmapLayout
