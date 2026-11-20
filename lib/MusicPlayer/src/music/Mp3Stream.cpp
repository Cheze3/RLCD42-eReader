// minimp3 (CC0, third_party/minimp3.h) built without SIMD and without layers 1 and 2: smaller, and
// the same code path on the device and in the host tests.
#ifndef MINIMP3_ONLY_MP3
#define MINIMP3_ONLY_MP3
#endif
#ifndef MINIMP3_NO_SIMD
#define MINIMP3_NO_SIMD
#endif
#define MINIMP3_IMPLEMENTATION
#include "Mp3Stream.h"

#include <algorithm>
#include <cstring>

namespace music {

namespace {
constexpr size_t kLowWater = 4096;  // refill before a frame could straddle the end of the buffer

uint32_t bigEndian32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}
}  // namespace

void Mp3Stream::resetBuffer() {
  bufLen = 0;
  bufPos = 0;
  eof = false;
  pendingSamples = 0;
  mp3dec_init(&dec);
}

void Mp3Stream::refill() {
  if (bufPos > 0) {
    memmove(buf, buf + bufPos, bufLen - bufPos);
    bufLen -= bufPos;
    bufPos = 0;
  }
  while (bufLen < kInputBytes && !eof) {
    const int n = src->read(buf + bufLen, kInputBytes - bufLen);
    if (n <= 0) {
      eof = true;
    } else {
      bufLen += static_cast<size_t>(n);
    }
  }
}

int Mp3Stream::decodeNext(mp3dec_frame_info_t& info, size_t& frameStart) {
  skipped = 0;
  for (;;) {
    if (cancel != nullptr && cancel->load()) return 0;
    if (bufLen - bufPos < kLowWater && !eof) refill();
    const size_t avail = bufLen - bufPos;
    if (avail == 0) return 0;
    const size_t before = bufPos;
    const int samples = mp3dec_decode_frame(&dec, buf + bufPos, static_cast<int>(avail), pcm, &info);
    bufPos += static_cast<size_t>(info.frame_bytes);
    if (samples > 0) {
      frameStart = before + static_cast<size_t>(info.frame_offset);
      return samples;
    }
    skipped += static_cast<size_t>(info.frame_bytes);
    if (info.frame_bytes == 0) {
      // No frame in what is buffered: more data, or garbage if the buffer is already full.
      if (eof) return 0;
      if (avail >= kInputBytes) {
        bufPos += avail - 4;
        skipped += avail - 4;
      }
    }
    if (skipped > kMaxResyncBytes) return 0;  // not an MP3, or the stream is badly damaged
  }
}

// The first frame of a VBR/CBR file written by LAME and others carries a "Xing" or "Info" header
// with the frame count and, for VBR, a 100-entry seek table. It decodes as silence.
bool Mp3Stream::readXing(const uint8_t* frame, size_t length) {
  if (length < 40 || frame[0] != 0xFF) return false;
  const int version = (frame[1] >> 3) & 3;  // 3 = MPEG-1
  const bool mono = (frame[3] >> 6) == 3;
  const size_t tagAt = version == 3 ? (mono ? 21 : 36) : (mono ? 13 : 21);
  if (length < tagAt + 8 || (memcmp(frame + tagAt, "Xing", 4) != 0 && memcmp(frame + tagAt, "Info", 4) != 0)) {
    return false;
  }

  const uint32_t flags = bigEndian32(frame + tagAt + 4);
  size_t at = tagAt + 8;
  uint32_t frames = 0;
  if (flags & 1) {
    if (length < at + 4) return true;
    frames = bigEndian32(frame + at);
    at += 4;
  }
  if (flags & 2) {
    if (length < at + 4) return true;
    audioBytes = bigEndian32(frame + at);
    at += 4;
  }
  if ((flags & 4) && length >= at + 100) {
    memcpy(toc, frame + at, 100);
    hasToc = true;
  }
  if (frames > 0 && hz > 0) {
    samplesPerFrame = version == 3 ? 1152 : 576;
    durationMsValue = static_cast<uint32_t>(static_cast<uint64_t>(frames) * samplesPerFrame * 1000 / hz);
  }
  return true;
}

bool Mp3Stream::open(ByteSource& source) {
  src = &source;
  trackTag = TrackInfo{};
  hz = 0;
  channelCount = 0;
  kbps = 0;
  durationMsValue = 0;
  audioBytes = 0;
  hasToc = false;
  baseMs = 0;
  framesSinceSeek = 0;

  readId3(source, trackTag);
  if (!source.seek(trackTag.audioStart)) return false;
  resetBuffer();

  mp3dec_frame_info_t info;
  size_t frameStart = 0;
  int samples = decodeNext(info, frameStart);
  if (samples <= 0) return false;
  hz = static_cast<uint32_t>(info.hz);
  channelCount = static_cast<uint8_t>(info.channels);
  kbps = static_cast<uint16_t>(info.bitrate_kbps);

  const size_t fileSize = source.size();
  if (fileSize > trackTag.audioStart) audioBytes = fileSize - trackTag.audioStart;
  const uint32_t cbrDuration = kbps > 0 ? static_cast<uint32_t>(audioBytes * 8 / kbps) : 0;
  const size_t frameLength = static_cast<size_t>(info.frame_bytes) - static_cast<size_t>(info.frame_offset);
  // A Xing/Info frame is silence, not music: drop it and start at the next frame.
  const bool xingFrame = readXing(buf + frameStart, frameLength);
  if (durationMsValue == 0) durationMsValue = cbrDuration;

  pendingSamples = xingFrame ? 0 : samples;
  return true;
}

size_t Mp3Stream::readFrames(int16_t* out) {
  int samples = pendingSamples;
  pendingSamples = 0;
  int channelsNow = channelCount;
  if (samples == 0) {
    mp3dec_frame_info_t info;
    size_t frameStart;
    samples = decodeNext(info, frameStart);
    if (samples <= 0) return 0;
    channelsNow = info.channels;
  }
  if (channelsNow == 2) {
    memcpy(out, pcm, static_cast<size_t>(samples) * 2 * sizeof(int16_t));
  } else {
    for (int i = 0; i < samples; ++i) out[2 * i] = out[2 * i + 1] = pcm[i];
  }
  framesSinceSeek += static_cast<uint64_t>(samples);
  return static_cast<size_t>(samples);
}

bool Mp3Stream::seekMs(uint32_t ms) {
  if (src == nullptr || hz == 0) return false;
  if (durationMsValue > 0 && ms > durationMsValue) ms = durationMsValue;
  uint64_t offset;
  if (hasToc && durationMsValue > 0 && audioBytes > 0) {
    const uint64_t scaled = static_cast<uint64_t>(ms) * 100 * 256 / durationMsValue;  // percent * 256
    const size_t i = static_cast<size_t>(std::min<uint64_t>(scaled / 256, 99));
    const uint64_t next = i < 99 ? toc[i + 1] : 256;
    const uint64_t lerp = toc[i] * 256 + (next - toc[i]) * (scaled % 256);  // 0..65536
    offset = lerp * audioBytes / 65536;
  } else {
    offset = static_cast<uint64_t>(ms) * kbps / 8;
  }
  if (audioBytes > 0 && offset >= audioBytes) offset = audioBytes > 4096 ? audioBytes - 4096 : 0;
  if (!src->seek(trackTag.audioStart + static_cast<size_t>(offset))) return false;
  resetBuffer();
  baseMs = ms;
  framesSinceSeek = 0;
  return true;
}

uint32_t Mp3Stream::positionMs() const {
  return baseMs + static_cast<uint32_t>(framesSinceSeek * 1000 / (hz ? hz : 1));
}

}  // namespace music
