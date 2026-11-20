#pragma once

#include <cstddef>
#include <cstdint>

#include "ByteSource.h"

namespace music {

struct TrackInfo {
  char title[64] = "";
  char artist[48] = "";
  char album[48] = "";
  uint32_t audioStart = 0;  // byte offset of the first audio byte (after the ID3v2 tag, if any)
};

// Reads the ID3v2 tag at the start of `src` (v2.2, v2.3 and v2.4 text frames TIT2/TPE1/TALB),
// converting text to UTF-8. Only the frames it needs are read, so a tag with large cover art costs
// a few small reads. Always sets audioStart; the text fields stay empty when there is no tag.
void readId3(ByteSource& src, TrackInfo& out);

}  // namespace music
