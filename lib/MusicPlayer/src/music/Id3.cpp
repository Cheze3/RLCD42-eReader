#include "Id3.h"

#include <cstring>

namespace music {

namespace {

constexpr size_t kMaxFrameRead = 256;

uint32_t syncsafe(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0] & 0x7F) << 21) | (static_cast<uint32_t>(p[1] & 0x7F) << 14) |
         (static_cast<uint32_t>(p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

uint32_t bigEndian(const uint8_t* p, int bytes) {
  uint32_t v = 0;
  for (int i = 0; i < bytes; ++i) v = (v << 8) | p[i];
  return v;
}

// Appends one code point as UTF-8; returns false when the buffer is full.
bool put(char* out, size_t cap, size_t& len, uint32_t cp) {
  char tmp[4];
  size_t n;
  if (cp < 0x80) {
    tmp[0] = static_cast<char>(cp);
    n = 1;
  } else if (cp < 0x800) {
    tmp[0] = static_cast<char>(0xC0 | (cp >> 6));
    tmp[1] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 2;
  } else {
    tmp[0] = static_cast<char>(0xE0 | (cp >> 12));
    tmp[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    tmp[2] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 3;
  }
  if (len + n >= cap) return false;
  memcpy(out + len, tmp, n);
  len += n;
  return true;
}

// Frame payload = encoding byte + text. Encodings: 0 Latin-1, 1 UTF-16 with BOM, 2 UTF-16BE, 3 UTF-8.
void decodeText(const uint8_t* data, size_t len, char* out, size_t cap) {
  size_t outLen = 0;
  out[0] = '\0';
  if (len < 2) return;
  const uint8_t encoding = data[0];
  const uint8_t* p = data + 1;
  size_t n = len - 1;
  if (encoding == 0 || encoding == 3) {
    for (size_t i = 0; i < n && p[i] != 0; ++i) {
      if (encoding == 3) {
        if (outLen + 1 >= cap) {
          // Cut on a character boundary: drop the last character only if its bytes are incomplete.
          size_t lead = outLen;
          while (lead > 0 && (static_cast<uint8_t>(out[lead - 1]) & 0xC0) == 0x80) --lead;
          if (lead > 0) {
            const uint8_t b = static_cast<uint8_t>(out[lead - 1]);
            const size_t need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
            if (outLen - (lead - 1) < need) outLen = lead - 1;
          }
          break;
        }
        out[outLen++] = static_cast<char>(p[i]);
      } else if (!put(out, cap, outLen, p[i])) {
        break;
      }
    }
  } else {
    bool bigEndianText = encoding == 2;
    size_t i = 0;
    if (encoding == 1 && n >= 2) {
      if (p[0] == 0xFE && p[1] == 0xFF) {
        bigEndianText = true;
        i = 2;
      } else if (p[0] == 0xFF && p[1] == 0xFE) {
        i = 2;
      }
    }
    for (; i + 1 < n; i += 2) {
      const uint32_t cp = bigEndianText ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
      if (cp == 0 || !put(out, cap, outLen, cp)) break;
    }
  }
  out[outLen] = '\0';
}

}  // namespace

void readId3(ByteSource& src, TrackInfo& out) {
  out.audioStart = 0;
  uint8_t header[10];
  if (!src.seek(0) || src.read(header, 10) != 10 || memcmp(header, "ID3", 3) != 0) return;

  const uint8_t version = header[3];
  const bool footer = version == 4 && (header[5] & 0x10);
  const uint32_t tagSize = syncsafe(header + 6);
  const size_t tagEnd = 10 + static_cast<size_t>(tagSize);
  out.audioStart = static_cast<uint32_t>(tagEnd + (footer ? 10 : 0));
  if (version < 2 || version > 4) return;

  size_t pos = 10;
  // The extended header (v2.3/v2.4) comes first when flagged; skip it.
  if (version >= 3 && (header[5] & 0x40)) {
    uint8_t ext[4];
    if (!src.seek(pos) || src.read(ext, 4) != 4) return;
    pos += (version == 4 ? syncsafe(ext) : bigEndian(ext, 4) + 4);
  }

  const size_t idLen = version == 2 ? 3 : 4;
  const size_t headerLen = version == 2 ? 6 : 10;
  while (pos + headerLen <= tagEnd) {
    uint8_t fh[10];
    if (!src.seek(pos) || src.read(fh, headerLen) != static_cast<int>(headerLen)) return;
    if (fh[0] == 0) return;  // padding
    size_t size;
    if (version == 2) {
      size = bigEndian(fh + 3, 3);
    } else if (version == 3) {
      size = bigEndian(fh + 4, 4);
    } else {
      size = syncsafe(fh + 4);
    }
    const size_t dataPos = pos + headerLen;
    if (dataPos + size > tagEnd) return;

    char* dest = nullptr;
    size_t cap = 0;
    if (memcmp(fh, version == 2 ? "TT2" : "TIT2", idLen) == 0) {
      dest = out.title;
      cap = sizeof(out.title);
    } else if (memcmp(fh, version == 2 ? "TP1" : "TPE1", idLen) == 0) {
      dest = out.artist;
      cap = sizeof(out.artist);
    } else if (memcmp(fh, version == 2 ? "TAL" : "TALB", idLen) == 0) {
      dest = out.album;
      cap = sizeof(out.album);
    }
    if (dest != nullptr && size > 0 && size <= kMaxFrameRead) {
      uint8_t buf[kMaxFrameRead];
      if (src.seek(dataPos) && src.read(buf, size) == static_cast<int>(size)) decodeText(buf, size, dest, cap);
    }
    pos = dataPos + size;
  }
}

}  // namespace music
