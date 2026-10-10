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

// A partial page plans with tryPlace() over every glyph, then the read loop
// replays place() over the kept glyphs only. Both must yield the same offsets,
// all inside the chunks that were actually allocated.
void partialPlanMatchesReadPass() {
  constexpr uint32_t kChunkBytes = 4096;
  uint32_t state = 99;
  for (uint32_t allocated = 1; allocated <= 8; allocated++) {
    const uint32_t capacity = allocated * kChunkBytes;
    std::vector<uint32_t> lengths;
    for (int i = 0; i < 80; i++) lengths.push_back(nextRandom(state) % 1200);  // 0 = no bitmap
    std::vector<uint32_t> planned;
    std::vector<bool> kept;
    uint32_t cursor = 0;
    for (const uint32_t length : lengths) {
      if (length == 0) {
        kept.push_back(true);
        continue;
      }
      const uint32_t before = cursor;
      const bool fits = ChunkedBitmapLayout::tryPlace(cursor, length, kChunkBytes, capacity);
      kept.push_back(fits);
      if (!fits) {
        expect(cursor == before, "failed tryPlace moved the cursor", cursor);
        continue;
      }
      planned.push_back(cursor - length);
      expect(cursor <= capacity, "kept glyph ends past the allocated chunks", cursor);
    }
    const uint32_t plannedEnd = cursor;
    uint32_t replay = 0;
    size_t next = 0;
    for (size_t i = 0; i < lengths.size(); i++) {
      if (!kept[i] || lengths[i] == 0) continue;
      const uint32_t at = ChunkedBitmapLayout::place(replay, lengths[i], kChunkBytes);
      expect(next < planned.size() && planned[next] == at, "read pass offset differs from plan", at);
      expect(at / kChunkBytes < allocated, "read pass resolves past the allocated chunks", at);
      next++;
    }
    expect(next == planned.size(), "read pass placed a different glyph count", static_cast<uint32_t>(next));
    expect(replay == plannedEnd, "read pass ends elsewhere than the plan", replay);
    expect(ChunkedBitmapLayout::chunkCount(plannedEnd, kChunkBytes) <= allocated, "plan needs more chunks", allocated);
  }
}

// A glyph too big for the room left in the last chunk is dropped, but a later
// smaller one that still fits is kept.
void partialPlanKeepsSmallerGlyphsAfterADrop() {
  uint32_t cursor = 0;
  expect(ChunkedBitmapLayout::tryPlace(cursor, 3000, 4096, 4096), "first glyph rejected");
  expect(!ChunkedBitmapLayout::tryPlace(cursor, 1200, 4096, 4096), "glyph past capacity accepted", cursor);
  expect(cursor == 3000, "rejected glyph moved the cursor", cursor);
  expect(ChunkedBitmapLayout::tryPlace(cursor, 1000, 4096, 4096), "smaller glyph that fits rejected", cursor);
  expect(cursor == 4000, "smaller glyph misplaced", cursor);
  expect(ChunkedBitmapLayout::tryPlace(cursor, 96, 4096, 4096), "glyph exactly filling the last chunk rejected",
         cursor);
  expect(cursor == 4096, "exact-fit glyph misplaced", cursor);
}

}  // namespace

int main() {
  packedGlyphsStayInsideTheirChunks();
  exactFitDoesNotSkipAChunk();
  chunkSizedGlyphsGetOneChunkEach();
  unboundedChunkNeverPads();
  partialPlanMatchesReadPass();
  partialPlanKeepsSmallerGlyphsAfterADrop();
  if (failures != 0) {
    std::fprintf(stderr, "%d chunked bitmap layout check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("CHUNKED_BITMAP_LAYOUT_OK");
  return EXIT_SUCCESS;
}
