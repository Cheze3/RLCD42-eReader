#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "ByteSource.h"
#include "Id3.h"
#include "third_party/minimp3.h"

namespace music {

// Streams an MP3 file as 16-bit stereo PCM, one frame at a time. Owns the decoder state and a read
// buffer (about 29 KB in all), so allocate it once per player, not per track on the stack.
class Mp3Stream {
 public:
  static constexpr size_t kInputBytes = 16 * 1024;
  static constexpr size_t kMaxFrames = 1152;  // PCM frames (stereo pairs) per decoded MP3 frame
  // Bytes skipped looking for the next frame before the stream gives up, so a file that is not MP3 ends
  // quickly instead of being read to the end.
  static constexpr size_t kMaxResyncBytes = 128 * 1024;

  // When the flag becomes true, open() and readFrames() stop at the next frame boundary and return as if
  // the stream had ended. Lets the player's owner stop a decode in progress. The flag must outlive the stream.
  void setCancel(const std::atomic<bool>* flag) { cancel = flag; }

  // Reads the ID3 tag, finds the first audio frame and learns rate, channels, bitrate and length.
  // Returns false for a file with no decodable MP3 audio. `source` must outlive the stream.
  bool open(ByteSource& source);

  uint32_t sampleRate() const { return hz; }
  uint8_t channels() const { return channelCount; }
  uint16_t bitrateKbps() const { return kbps; }
  uint32_t durationMs() const { return durationMsValue; }
  const TrackInfo& tag() const { return trackTag; }

  // Decodes the next frame into `out` (room for kMaxFrames * 2 samples, interleaved L/R). Mono is
  // duplicated to both channels. Returns the PCM frame count, or 0 at the end of the stream.
  size_t readFrames(int16_t* out);

  // Jumps to `ms` (uses the Xing table for VBR files, the bitrate otherwise). Position is
  // approximate for files without a table.
  bool seekMs(uint32_t ms);
  uint32_t positionMs() const;

 private:
  void refill();
  void resetBuffer();
  // Decodes one frame into pcm; returns the sample count (per channel), 0 at the end.
  int decodeNext(mp3dec_frame_info_t& info, size_t& frameStart);
  // True when `frame` is a Xing/Info header frame (and records its frame count and seek table).
  bool readXing(const uint8_t* frame, size_t length);

  ByteSource* src = nullptr;
  mp3dec_t dec;
  uint8_t buf[kInputBytes];
  size_t bufLen = 0;
  size_t bufPos = 0;
  bool eof = false;
  size_t skipped = 0;  // bytes skipped since the last good frame
  const std::atomic<bool>* cancel = nullptr;
  mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  int pendingSamples = 0;

  TrackInfo trackTag;
  uint32_t hz = 0;
  uint8_t channelCount = 0;
  uint16_t kbps = 0;
  uint32_t durationMsValue = 0;
  uint32_t samplesPerFrame = 1152;
  uint64_t audioBytes = 0;
  uint8_t toc[100];
  bool hasToc = false;
  uint32_t baseMs = 0;
  uint64_t framesSinceSeek = 0;
};

}  // namespace music
