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

// Like place(), but only when the bitmap ends within `capacity` bytes (the
// allocated chunks); otherwise leaves `cursor` alone and returns false. Used
// to plan a partial page whose remaining bitmaps load on demand.
inline bool tryPlace(uint32_t& cursor, const uint32_t length, const uint32_t chunkBytes, const uint32_t capacity) {
  uint32_t end = cursor;
  place(end, length, chunkBytes);
  if (end > capacity) return false;
  cursor = end;
  return true;
}

inline uint32_t chunkCount(const uint32_t packedBytes, const uint32_t chunkBytes) {
  return (packedBytes + chunkBytes - 1) / chunkBytes;
}

template <typename Byte>
Byte* resolve(Byte* const* chunks, const uint32_t chunkBytes, const uint32_t offset) {
  return chunks[offset / chunkBytes] + offset % chunkBytes;
}

}  // namespace ChunkedBitmapLayout
