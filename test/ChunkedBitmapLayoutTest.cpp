#include <ChunkedBitmapLayout.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const char* message, const uint32_t detail = 0) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s (%u)\n", message, detail);
    failures++;
  }
}

uint32_t nextRandom(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state >> 8;
}

// Packs pseudo-random glyph sizes and checks the layout invariants the SD font
// relies on when it reads bitmaps into chunks and resolves them while drawing.
void packedGlyphsStayInsideTheirChunks() {
  constexpr uint32_t kChunkBytes = 4096;
  constexpr uint32_t kMaxGlyphBytes = 1200;  // above a 22pt CJK glyph
  uint32_t state = 12345;
  uint32_t cursor = 0;
  uint32_t previousEnd = 0;
  std::vector<uint32_t> offsets;
  std::vector<uint32_t> lengths;
  for (int i = 0; i < 5000; i++) {
    const uint32_t length = 1 + nextRandom(state) % kMaxGlyphBytes;
    const uint32_t at = ChunkedBitmapLayout::place(cursor, length, kChunkBytes);
    expect(at % kChunkBytes + length <= kChunkBytes, "glyph straddles a chunk boundary", at);
    expect(at >= previousEnd, "glyph overlaps the previous one", at);
    expect(at - previousEnd < kMaxGlyphBytes, "padding exceeds one glyph", at - previousEnd);
    previousEnd = at + length;
    offsets.push_back(at);
    lengths.push_back(length);
  }
  expect(cursor == previousEnd, "cursor does not end after the last glyph", cursor);
  const uint32_t chunks = ChunkedBitmapLayout::chunkCount(cursor, kChunkBytes);
  expect(offsets.back() / kChunkBytes < chunks, "last glyph lies beyond the allocated chunks", chunks);

  // Resolve against real allocations: every glyph gets its own fill byte and
  // must read back intact once all glyphs are written.
  std::vector<std::vector<uint8_t>> storage(chunks, std::vector<uint8_t>(kChunkBytes));
  std::vector<uint8_t*> chunkPointers;
  for (auto& chunk : storage) chunkPointers.push_back(chunk.data());
  for (size_t i = 0; i < offsets.size(); i++) {
    uint8_t* dest = ChunkedBitmapLayout::resolve(chunkPointers.data(), kChunkBytes, offsets[i]);
    for (uint32_t b = 0; b < lengths[i]; b++) dest[b] = static_cast<uint8_t>(i);
  }
  for (size_t i = 0; i < offsets.size(); i++) {
    const uint8_t* src = ChunkedBitmapLayout::resolve<const uint8_t>(chunkPointers.data(), kChunkBytes, offsets[i]);
    for (uint32_t b = 0; b < lengths[i]; b++) {
      if (src[b] != static_cast<uint8_t>(i)) {
        expect(false, "glyph bitmap was overwritten", static_cast<uint32_t>(i));
        break;
      }
    }
  }
}

void exactFitDoesNotSkipAChunk() {
  uint32_t cursor = 0;
  for (int i = 0; i < 4; i++) ChunkedBitmapLayout::place(cursor, 1024, 4096);
  expect(cursor == 4096, "four 1KB glyphs do not fill one 4KB chunk", cursor);
  expect(ChunkedBitmapLayout::chunkCount(cursor, 4096) == 1, "an exactly full chunk counts as two");
  expect(ChunkedBitmapLayout::place(cursor, 1, 4096) == 4096, "next glyph does not start the second chunk", cursor);
  expect(ChunkedBitmapLayout::chunkCount(0, 4096) == 0, "an empty page allocates a chunk");
}

void chunkSizedGlyphsGetOneChunkEach() {
  uint32_t cursor = 0;
  expect(ChunkedBitmapLayout::place(cursor, 100, 4096) == 0, "first glyph not at zero");
  expect(ChunkedBitmapLayout::place(cursor, 4096, 4096) == 4096, "full-chunk glyph not chunk aligned", cursor);
  expect(ChunkedBitmapLayout::place(cursor, 4096, 4096) == 8192, "second full-chunk glyph not chunk aligned", cursor);
  expect(ChunkedBitmapLayout::chunkCount(cursor, 4096) == 3, "full-chunk glyphs miscounted", cursor);
}

// The contiguous coverage arena packs with an unbounded chunk size: no padding.
void unboundedChunkNeverPads() {
  uint32_t cursor = 0;
  uint32_t expected = 0;
  uint32_t state = 7;
  for (int i = 0; i < 1000; i++) {
    const uint32_t length = 1 + nextRandom(state) % 900;
    expect(ChunkedBitmapLayout::place(cursor, length, UINT32_MAX) == expected, "arena layout padded", expected);
    expected += length;
  }
}

}  // namespace

int main() {
  packedGlyphsStayInsideTheirChunks();
  exactFitDoesNotSkipAChunk();
  chunkSizedGlyphsGetOneChunkEach();
  unboundedChunkNeverPads();
  if (failures != 0) {
    std::fprintf(stderr, "%d chunked bitmap layout check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("CHUNKED_BITMAP_LAYOUT_OK");
  return EXIT_SUCCESS;
}
