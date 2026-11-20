#include <gtest/gtest.h>
#include <music/Id3.h>
#include <music/Mp3Stream.h>
#include <music/PlayQueue.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace music;

namespace {

class MemorySource : public ByteSource {
 public:
  explicit MemorySource(std::vector<uint8_t> bytes) : data(std::move(bytes)) {}
  static MemorySource fromFile(const char* name) {
    std::ifstream in(std::string(MUSIC_TEST_DATA) + "/" + name, std::ios::binary);
    EXPECT_TRUE(in.good()) << name;
    return MemorySource(std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {}));
  }
  int read(uint8_t* dst, size_t n) override {
    const size_t left = data.size() - pos;
    const size_t take = std::min(n, left);
    memcpy(dst, data.data() + pos, take);
    pos += take;
    bytesRead += take;
    return static_cast<int>(take);
  }
  size_t bytesRead = 0;
  bool seek(size_t p) override {
    if (p > data.size()) return false;
    pos = p;
    return true;
  }
  size_t size() const override { return data.size(); }

 private:
  std::vector<uint8_t> data;
  size_t pos = 0;
};

void putSyncsafe(std::vector<uint8_t>& v, uint32_t n) {
  v.push_back((n >> 21) & 0x7F);
  v.push_back((n >> 14) & 0x7F);
  v.push_back((n >> 7) & 0x7F);
  v.push_back(n & 0x7F);
}

// Builds a tag with one text frame. version 2: 3-char id + 3-byte size; 3: BE size; 4: syncsafe size.
std::vector<uint8_t> makeTag(int version, const char* id, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> frame;
  for (const char* c = id; *c; ++c) frame.push_back(static_cast<uint8_t>(*c));
  const uint32_t size = static_cast<uint32_t>(payload.size());
  if (version == 2) {
    frame.insert(frame.end(),
                 {static_cast<uint8_t>(size >> 16), static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size)});
  } else if (version == 3) {
    frame.insert(frame.end(), {0, 0, static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size), 0, 0});
  } else {
    putSyncsafe(frame, size);
    frame.insert(frame.end(), {0, 0});
  }
  frame.insert(frame.end(), payload.begin(), payload.end());
  std::vector<uint8_t> tag = {'I', 'D', '3', static_cast<uint8_t>(version), 0, 0};
  putSyncsafe(tag, static_cast<uint32_t>(frame.size()) + 20);  // 20 bytes of padding
  tag.insert(tag.end(), frame.begin(), frame.end());
  tag.insert(tag.end(), 20, 0);
  return tag;
}

std::vector<uint8_t> text(uint8_t encoding, std::initializer_list<uint8_t> bytes) {
  std::vector<uint8_t> v = {encoding};
  v.insert(v.end(), bytes.begin(), bytes.end());
  return v;
}

// Zero-crossing rate of the left channel, as Hz.
double toneHz(const std::vector<int16_t>& left, uint32_t rate) {
  int crossings = 0;
  for (size_t i = 1; i < left.size(); ++i) crossings += (left[i - 1] < 0) != (left[i] < 0);
  return crossings / 2.0 * rate / left.size();
}

struct Decoded {
  std::vector<int16_t> left;
  size_t frames = 0;
};

Decoded decodeAll(Mp3Stream& stream, size_t maxFrames = 10000000) {
  Decoded d;
  std::vector<int16_t> pcm(Mp3Stream::kMaxFrames * 2);
  while (d.frames < maxFrames) {
    const size_t n = stream.readFrames(pcm.data());
    if (n == 0) break;
    for (size_t i = 0; i < n; ++i) d.left.push_back(pcm[2 * i]);
    d.frames += n;
  }
  return d;
}

}  // namespace

TEST(Id3, ReadsUtf16TagFromARealFile) {
  auto src = MemorySource::fromFile("mono_44100_cbr64_tagged.mp3");
  TrackInfo info;
  readId3(src, info);
  EXPECT_STREQ(info.title, "Mono Title");
  EXPECT_STREQ(info.artist, "The Artist");
  EXPECT_STREQ(info.album, "An Album");
  EXPECT_GT(info.audioStart, 0u);
}

TEST(Id3, Latin1V23ConvertsToUtf8) {
  MemorySource src(makeTag(3, "TIT2", text(0, {'C', 'a', 'f', 0xE9, 0})));
  TrackInfo info;
  readId3(src, info);
  EXPECT_STREQ(info.title, "Caf\xC3\xA9");
}

TEST(Id3, V22AndV24Frames) {
  MemorySource v22(makeTag(2, "TP1", text(0, {'A', 'B'})));
  TrackInfo a;
  readId3(v22, a);
  EXPECT_STREQ(a.artist, "AB");

  MemorySource v24(makeTag(4, "TALB", text(3, {'X', 0xE2, 0x82, 0xAC})));  // UTF-8 euro sign
  TrackInfo b;
  readId3(v24, b);
  EXPECT_STREQ(b.album, "X\xE2\x82\xAC");
}

TEST(Id3, LongUtf8TitleIsCutOnACharacterBoundary) {
  std::vector<uint8_t> text = {3};                                     // UTF-8
  for (int i = 0; i < 70; ++i) text.insert(text.end(), {0xC3, 0xA9});  // 70 x "e acute" = 140 bytes
  MemorySource src(makeTag(4, "TIT2", text));
  TrackInfo info;
  readId3(src, info);
  const size_t len = strlen(info.title);
  EXPECT_LT(len, sizeof(info.title));
  EXPECT_GT(len, 0u);
  EXPECT_EQ(len % 2, 0u);  // whole two-byte characters only
  for (size_t i = 0; i < len; i += 2) {
    EXPECT_EQ(static_cast<uint8_t>(info.title[i]), 0xC3);
    EXPECT_EQ(static_cast<uint8_t>(info.title[i + 1]), 0xA9);
  }
}

TEST(Id3, NoTagMeansAudioStartsAtZero) {
  MemorySource src(std::vector<uint8_t>(64, 0xAB));
  TrackInfo info;
  info.audioStart = 99;
  readId3(src, info);
  EXPECT_EQ(info.audioStart, 0u);
  EXPECT_STREQ(info.title, "");
}

TEST(Id3, OversizedFrameIsSkippedNotRead) {
  std::vector<uint8_t> big(5000, 'x');
  big[0] = 0;
  MemorySource src(makeTag(3, "TIT2", big));
  TrackInfo info;
  readId3(src, info);
  EXPECT_STREQ(info.title, "");
  EXPECT_GT(info.audioStart, 5000u);
}

TEST(Mp3Stream, OpensEachFixtureWithTheRightFormat) {
  struct Case {
    const char* file;
    uint32_t hz;
    uint8_t channels;
  } cases[] = {{"mono_44100_cbr64_tagged.mp3", 44100, 1},
               {"stereo_48000_cbr128.mp3", 48000, 2},
               {"stereo_44100_vbr.mp3", 44100, 2}};
  for (const Case& c : cases) {
    auto src = MemorySource::fromFile(c.file);
    Mp3Stream stream;
    ASSERT_TRUE(stream.open(src)) << c.file;
    EXPECT_EQ(stream.sampleRate(), c.hz) << c.file;
    EXPECT_EQ(stream.channels(), c.channels) << c.file;
    EXPECT_NEAR(static_cast<double>(stream.durationMs()), 3000.0, 120.0) << c.file;
  }
}

TEST(Mp3Stream, DecodesTheWholeFileAtTheRightLengthAndPitch) {
  for (const char* file : {"mono_44100_cbr64_tagged.mp3", "stereo_48000_cbr128.mp3", "stereo_44100_vbr.mp3"}) {
    auto src = MemorySource::fromFile(file);
    Mp3Stream stream;
    ASSERT_TRUE(stream.open(src)) << file;
    const Decoded d = decodeAll(stream);
    const double seconds = static_cast<double>(d.frames) / stream.sampleRate();
    EXPECT_NEAR(seconds, 3.0, 0.15) << file;
    // Skip the encoder delay, then the left channel should be a 440 Hz sine.
    std::vector<int16_t> body(d.left.begin() + 4000, d.left.end() - 2000);
    EXPECT_NEAR(toneHz(body, stream.sampleRate()), 440.0, 6.0) << file;
    EXPECT_EQ(stream.readFrames(std::vector<int16_t>(Mp3Stream::kMaxFrames * 2).data()), 0u) << file;
  }
}

TEST(Mp3Stream, MonoIsDuplicatedToBothChannels) {
  auto src = MemorySource::fromFile("mono_44100_cbr64_tagged.mp3");
  Mp3Stream stream;
  ASSERT_TRUE(stream.open(src));
  std::vector<int16_t> pcm(Mp3Stream::kMaxFrames * 2);
  size_t n;
  do {
    n = stream.readFrames(pcm.data());
  } while (n > 0 && stream.positionMs() < 500);
  ASSERT_GT(n, 0u);
  for (size_t i = 0; i < n; ++i) EXPECT_EQ(pcm[2 * i], pcm[2 * i + 1]);
}

TEST(Mp3Stream, SeekLandsNearTheRequestedTimeInCbrAndVbr) {
  for (const char* file : {"stereo_48000_cbr128.mp3", "stereo_44100_vbr.mp3"}) {
    auto src = MemorySource::fromFile(file);
    Mp3Stream stream;
    ASSERT_TRUE(stream.open(src)) << file;
    ASSERT_TRUE(stream.seekMs(1500)) << file;
    EXPECT_EQ(stream.positionMs(), 1500u) << file;
    const Decoded d = decodeAll(stream);
    const double played = static_cast<double>(d.frames) / stream.sampleRate();
    EXPECT_NEAR(played, 1.5, 0.2) << file;  // about half the file is left
    std::vector<int16_t> body(d.left.begin() + 3000, d.left.end() - 2000);
    EXPECT_NEAR(toneHz(body, stream.sampleRate()), 440.0, 8.0) << file;
    EXPECT_NEAR(static_cast<double>(stream.positionMs()), 3000.0, 150.0) << file;
  }
}

TEST(Mp3Stream, SeekPastTheEndClampsAndEnds) {
  auto src = MemorySource::fromFile("stereo_48000_cbr128.mp3");
  Mp3Stream stream;
  ASSERT_TRUE(stream.open(src));
  ASSERT_TRUE(stream.seekMs(999999));
  EXPECT_LE(decodeAll(stream).frames, 8000u);  // at most a fraction of a second
}

TEST(Mp3Stream, GivesUpOnALargeNonMp3FileWithoutReadingItAll) {
  MemorySource src(std::vector<uint8_t>(2 * 1024 * 1024, 0x55));
  Mp3Stream stream;
  EXPECT_FALSE(stream.open(src));
  // It stops after the resync limit plus what the buffer held, not after the whole 2 MB.
  EXPECT_LE(src.bytesRead, Mp3Stream::kMaxResyncBytes + 3 * Mp3Stream::kInputBytes);
}

TEST(Mp3Stream, CancelStopsOpenAndDecoding) {
  std::atomic<bool> cancel{true};
  auto src = MemorySource::fromFile("stereo_48000_cbr128.mp3");
  Mp3Stream stream;
  stream.setCancel(&cancel);
  EXPECT_FALSE(stream.open(src));  // cancelled before the first frame

  cancel = false;
  auto src2 = MemorySource::fromFile("stereo_48000_cbr128.mp3");
  Mp3Stream stream2;
  stream2.setCancel(&cancel);
  ASSERT_TRUE(stream2.open(src2));
  std::vector<int16_t> pcm(Mp3Stream::kMaxFrames * 2);
  EXPECT_GT(stream2.readFrames(pcm.data()), 0u);
  cancel = true;
  EXPECT_EQ(stream2.readFrames(pcm.data()), 0u);  // stops at the next frame
}

TEST(Mp3Stream, RejectsNonMp3Data) {
  MemorySource src(std::vector<uint8_t>(40000, 0x55));
  Mp3Stream stream;
  EXPECT_FALSE(stream.open(src));
}

TEST(PlayQueue, PlaysInOrderAndStopsAtTheEndWithRepeatOff) {
  Rng rng(1);
  PlayQueue q;
  q.reset(3, 0, false, rng);
  EXPECT_EQ(q.current(), 0u);
  EXPECT_TRUE(q.next(Repeat::Off, true));
  EXPECT_EQ(q.current(), 1u);
  EXPECT_TRUE(q.next(Repeat::Off, true));
  EXPECT_EQ(q.current(), 2u);
  EXPECT_FALSE(q.next(Repeat::Off, true));  // finished by itself: stop
  EXPECT_EQ(q.current(), 2u);
  EXPECT_TRUE(q.next(Repeat::Off, false));  // user pressed next: wrap
  EXPECT_EQ(q.current(), 0u);
}

TEST(PlayQueue, RepeatModes) {
  Rng rng(1);
  PlayQueue q;
  q.reset(2, 1, false, rng);
  EXPECT_EQ(q.current(), 1u);
  EXPECT_TRUE(q.next(Repeat::One, true));
  EXPECT_EQ(q.current(), 1u);  // same track again
  EXPECT_TRUE(q.next(Repeat::All, true));
  EXPECT_EQ(q.current(), 0u);  // wrapped
}

TEST(PlayQueue, StartsAtTheChosenTrackAndPreviousStopsAtTheFirst) {
  Rng rng(1);
  PlayQueue q;
  q.reset(5, 3, false, rng);
  EXPECT_EQ(q.current(), 3u);
  q.previous();
  EXPECT_EQ(q.current(), 2u);
  q.reset(5, 0, false, rng);
  q.previous();
  EXPECT_EQ(q.current(), 0u);
}

TEST(PlayQueue, ShuffleVisitsEveryTrackOnceStartingWithTheChosenOne) {
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    Rng rng(seed);
    PlayQueue q;
    q.reset(12, 7, true, rng);
    EXPECT_EQ(q.current(), 7u);
    std::set<size_t> seen = {q.current()};
    while (q.next(Repeat::Off, true)) seen.insert(q.current());
    EXPECT_EQ(seen.size(), 12u);
  }
}

TEST(PlayQueue, TogglingShuffleKeepsTheCurrentTrack) {
  Rng rng(5);
  PlayQueue q;
  q.reset(8, 2, false, rng);
  q.next(Repeat::Off, true);  // now on 3
  q.setShuffle(true, rng);
  EXPECT_EQ(q.current(), 3u);
  q.setShuffle(false, rng);
  EXPECT_EQ(q.current(), 3u);
  EXPECT_TRUE(q.next(Repeat::Off, true));
  EXPECT_EQ(q.current(), 4u);
}

TEST(PlayQueue, EmptyQueueIsSafe) {
  Rng rng(1);
  PlayQueue q;
  q.reset(0, 0, true, rng);
  EXPECT_TRUE(q.empty());
  EXPECT_FALSE(q.next(Repeat::All, false));
  EXPECT_EQ(q.current(), 0u);
}
