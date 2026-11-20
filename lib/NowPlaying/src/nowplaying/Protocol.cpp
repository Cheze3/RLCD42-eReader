#include "Protocol.h"

#include <cstdlib>
#include <cstring>

namespace nowplaying {

namespace {

int sextet(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// Parses a non-negative decimal number occupying the whole of [s, s+len).
bool parseUnsigned(const char* s, size_t len, uint32_t& out) {
  if (len == 0 || len > 9) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < len; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + static_cast<uint32_t>(s[i] - '0');
  }
  out = v;
  return true;
}

// Splits `line` at single spaces into at most `max` fields; returns the field count.
size_t splitFields(const char* line, const char** starts, size_t* lengths, size_t max) {
  size_t n = 0;
  const char* p = line;
  while (*p && n < max) {
    const char* end = strchr(p, ' ');
    const size_t len = end ? static_cast<size_t>(end - p) : strlen(p);
    starts[n] = p;
    lengths[n] = len;
    ++n;
    if (!end) break;
    p = end + 1;
  }
  return n;
}

bool is(const char* field, size_t len, const char* word) {
  return strlen(word) == len && memcmp(field, word, len) == 0;
}

// Copies decoded bytes into `text`, cutting on a UTF-8 character boundary and dropping control bytes.
void setText(Message& out, const uint8_t* bytes, size_t len) {
  size_t n = 0;
  for (size_t i = 0; i < len && n + 1 < kTextBytes; ++i) {
    if (bytes[i] < 0x20) continue;
    out.text[n++] = static_cast<char>(bytes[i]);
  }
  // A cut can leave a character without all its continuation bytes: drop the partial character.
  if (n > 0) {
    size_t lead = n;
    while (lead > 0 && (static_cast<uint8_t>(out.text[lead - 1]) & 0xC0) == 0x80) --lead;
    if (lead > 0) {
      const uint8_t b = static_cast<uint8_t>(out.text[lead - 1]);
      const size_t need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
      if (n - (lead - 1) < need) n = lead - 1;
    }
  }
  out.text[n] = '\0';
}

}  // namespace

size_t base64Decode(const char* in, size_t inLen, uint8_t* out, size_t outCap) {
  if (inLen % 4 != 0) return SIZE_MAX;
  size_t o = 0;
  for (size_t i = 0; i < inLen; i += 4) {
    const bool pad2 = in[i + 2] == '=';
    const bool pad1 = in[i + 3] == '=';
    if ((pad2 && !pad1) || ((pad1 || pad2) && i + 4 != inLen)) return SIZE_MAX;
    const int a = sextet(in[i]), b = sextet(in[i + 1]);
    const int c = pad2 ? 0 : sextet(in[i + 2]);
    const int d = pad1 ? 0 : sextet(in[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0) return SIZE_MAX;
    const uint32_t v = static_cast<uint32_t>(a << 18 | b << 12 | c << 6 | d);
    const size_t produced = pad2 ? 1 : pad1 ? 2 : 3;
    if (o + produced > outCap) return SIZE_MAX;
    out[o++] = static_cast<uint8_t>(v >> 16);
    if (produced > 1) out[o++] = static_cast<uint8_t>(v >> 8);
    if (produced > 2) out[o++] = static_cast<uint8_t>(v);
  }
  return o;
}

bool parseLine(const char* line, Message& out) {
  out.type = MessageType::None;
  const char* f[5];
  size_t len[5];
  const size_t n = splitFields(line, f, len, 5);
  if (n < 2 || !is(f[0], len[0], "NP")) return false;

  if (is(f[1], len[1], "PING") && n == 2) {
    out.type = MessageType::Ping;
    return true;
  }
  if (is(f[1], len[1], "NOART") && n == 2) {
    out.type = MessageType::NoArt;
    return true;
  }
  if (is(f[1], len[1], "ARTEND") && n == 2) {
    out.type = MessageType::ArtEnd;
    return true;
  }

  if ((is(f[1], len[1], "TITLE") || is(f[1], len[1], "ARTIST") || is(f[1], len[1], "ALBUM")) && n <= 3) {
    uint8_t bytes[kTextBytes * 2];
    size_t got = 0;
    if (n == 3) {
      got = base64Decode(f[2], len[2], bytes, sizeof(bytes));
      if (got == SIZE_MAX) return false;
    }
    out.type = is(f[1], len[1], "TITLE")    ? MessageType::Title
               : is(f[1], len[1], "ARTIST") ? MessageType::Artist
                                            : MessageType::Album;
    setText(out, bytes, got);
    return true;
  }

  if (is(f[1], len[1], "STATE") && n == 5) {
    PlayState state;
    if (is(f[2], len[2], "playing")) {
      state = PlayState::Playing;
    } else if (is(f[2], len[2], "paused")) {
      state = PlayState::Paused;
    } else if (is(f[2], len[2], "stopped")) {
      state = PlayState::Stopped;
    } else {
      return false;
    }
    uint32_t pos, dur;
    if (!parseUnsigned(f[3], len[3], pos) || !parseUnsigned(f[4], len[4], dur)) return false;
    out.type = MessageType::State;
    out.state = state;
    out.positionSec = pos;
    out.durationSec = dur;
    return true;
  }

  if (is(f[1], len[1], "ART") && n == 5) {
    uint32_t w, h, total;
    if (!parseUnsigned(f[2], len[2], w) || !parseUnsigned(f[3], len[3], h) || !parseUnsigned(f[4], len[4], total))
      return false;
    if (w == 0 || h == 0 || w > 256 || h > 256 || total != ((w + 7) / 8) * h || total > kMaxArtBytes) return false;
    out.type = MessageType::ArtBegin;
    out.width = static_cast<uint16_t>(w);
    out.height = static_cast<uint16_t>(h);
    out.totalBytes = total;
    return true;
  }

  if (is(f[1], len[1], "ARTDATA") && n == 4) {
    uint32_t offset;
    if (!parseUnsigned(f[2], len[2], offset) || offset >= kMaxArtBytes) return false;
    const size_t got = base64Decode(f[3], len[3], out.data, kMaxChunkBytes);
    if (got == SIZE_MAX || got == 0 || offset + got > kMaxArtBytes) return false;
    out.type = MessageType::ArtData;
    out.offset = offset;
    out.dataLen = got;
    return true;
  }
  return false;
}

}  // namespace nowplaying
