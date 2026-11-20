#include <gtest/gtest.h>
#include <nowplaying/Protocol.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace nowplaying;

namespace {

// The lines written by tools/nowplaying/nowplaying_protocol.py, with the "CMD:" prefix removed (the
// board strips it before parsing).
std::vector<std::string> sessionLines() {
  std::ifstream in(std::string(NOWPLAYING_TEST_DATA) + "/session.txt");
  EXPECT_TRUE(in.good());
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    EXPECT_EQ(line.rfind("CMD:", 0), 0u);
    lines.push_back(line.substr(4));
  }
  return lines;
}

bool parse(const std::string& line, Message& m) { return parseLine(line.c_str(), m); }

}  // namespace

TEST(NowPlayingProtocol, ParsesTheLinesTheComputerProgramWrites) {
  const auto lines = sessionLines();
  ASSERT_EQ(lines.size(), 11u);
  Message m;

  ASSERT_TRUE(parse(lines[0], m));
  EXPECT_EQ(m.type, MessageType::Title);
  EXPECT_STREQ(m.text, "Forgive Me");
  ASSERT_TRUE(parse(lines[1], m));
  EXPECT_EQ(m.type, MessageType::Artist);
  EXPECT_STREQ(m.text, "imzoo");
  ASSERT_TRUE(parse(lines[2], m));
  EXPECT_EQ(m.type, MessageType::Album);
  EXPECT_STREQ(m.text, "Forgive Me - Single");
  ASSERT_TRUE(parse(lines[3], m));
  EXPECT_STREQ(m.text,
               "Caf\xC3\xA9 \xE2\x80\x93 \xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88");  // accents, dash and katakana survive

  ASSERT_TRUE(parse(lines[4], m));
  EXPECT_EQ(m.type, MessageType::State);
  EXPECT_EQ(m.state, PlayState::Playing);
  EXPECT_EQ(m.positionSec, 19u);
  EXPECT_EQ(m.durationSec, 197u);
  ASSERT_TRUE(parse(lines[5], m));
  EXPECT_EQ(m.state, PlayState::Paused);

  ASSERT_TRUE(parse(lines[6], m));
  EXPECT_EQ(m.type, MessageType::ArtBegin);
  EXPECT_EQ(m.width, 16);
  EXPECT_EQ(m.height, 8);
  EXPECT_EQ(m.totalBytes, 16u);
  ASSERT_TRUE(parse(lines[7], m));
  EXPECT_EQ(m.type, MessageType::ArtData);
  EXPECT_EQ(m.offset, 0u);
  ASSERT_EQ(m.dataLen, 16u);
  for (int i = 0; i < 16; ++i) EXPECT_EQ(m.data[i], i);
  ASSERT_TRUE(parse(lines[8], m));
  EXPECT_EQ(m.type, MessageType::ArtEnd);
  ASSERT_TRUE(parse(lines[9], m));
  EXPECT_EQ(m.type, MessageType::NoArt);
  ASSERT_TRUE(parse(lines[10], m));
  EXPECT_EQ(m.type, MessageType::Ping);
}

TEST(NowPlayingProtocol, Base64DecodesWithAndWithoutPadding) {
  uint8_t out[8];
  EXPECT_EQ(base64Decode("TWFu", 4, out, sizeof(out)), 3u);
  EXPECT_EQ(memcmp(out, "Man", 3), 0);
  EXPECT_EQ(base64Decode("TWE=", 4, out, sizeof(out)), 2u);
  EXPECT_EQ(base64Decode("TQ==", 4, out, sizeof(out)), 1u);
  EXPECT_EQ(base64Decode("", 0, out, sizeof(out)), 0u);
}

TEST(NowPlayingProtocol, Base64RejectsBadInput) {
  uint8_t out[8];
  EXPECT_EQ(base64Decode("TWF", 3, out, sizeof(out)), SIZE_MAX);       // length not a multiple of 4
  EXPECT_EQ(base64Decode("TW!u", 4, out, sizeof(out)), SIZE_MAX);      // illegal character
  EXPECT_EQ(base64Decode("T=Fu", 4, out, sizeof(out)), SIZE_MAX);      // padding in the middle
  EXPECT_EQ(base64Decode("TQ==TWFu", 8, out, sizeof(out)), SIZE_MAX);  // padding before the end
  EXPECT_EQ(base64Decode("TWFuTWFu", 8, out, 5), SIZE_MAX);            // output too small
}

TEST(NowPlayingProtocol, RejectsMalformedAndUnknownLines) {
  Message m;
  EXPECT_FALSE(parseLine("", m));
  EXPECT_FALSE(parseLine("NP", m));
  EXPECT_FALSE(parseLine("XX TITLE AAAA", m));
  EXPECT_FALSE(parseLine("NP DANCE", m));
  EXPECT_FALSE(parseLine("NP STATE playing 1", m));
  EXPECT_FALSE(parseLine("NP STATE sleeping 1 2", m));
  EXPECT_FALSE(parseLine("NP STATE playing -1 2", m));
  EXPECT_FALSE(parseLine("NP STATE playing 1 x", m));
  EXPECT_FALSE(parseLine("NP TITLE !!!!", m));
  EXPECT_FALSE(parseLine("NP PING extra", m));
  EXPECT_EQ(m.type, MessageType::None);
}

TEST(NowPlayingProtocol, ArtLimitsAreEnforced) {
  Message m;
  EXPECT_TRUE(parseLine("NP ART 240 240 7200", m));
  EXPECT_TRUE(parseLine("NP ART 256 256 8192", m));
  EXPECT_FALSE(parseLine("NP ART 257 8 264", m));     // too wide
  EXPECT_FALSE(parseLine("NP ART 240 240 7000", m));  // byte count does not match the size
  EXPECT_FALSE(parseLine("NP ART 0 8 0", m));
  EXPECT_FALSE(parseLine("NP ART 16 8 17", m));
  EXPECT_FALSE(parseLine("NP ARTDATA 8190 AAECAwQF", m));  // 6 bytes would run past the maximum
  EXPECT_FALSE(parseLine("NP ARTDATA 99999 AAAA", m));
}

TEST(NowPlayingProtocol, LongTextIsCutOnACharacterBoundary) {
  // 40 three-byte characters (120 bytes) encoded by hand: base64 of 0xE3 0x83 0x86 repeated.
  std::string encoded;
  for (int i = 0; i < 40; ++i) encoded += "44OG";
  Message m;
  ASSERT_TRUE(parseLine(("NP TITLE " + encoded).c_str(), m));
  const size_t len = strlen(m.text);
  EXPECT_LT(len, kTextBytes);
  EXPECT_EQ(len % 3, 0u);  // whole characters only
  EXPECT_EQ(len, 93u);
}

TEST(NowPlayingProtocol, ControlBytesAreStrippedFromText) {
  // "A\nB" = QQpC
  Message m;
  ASSERT_TRUE(parseLine("NP ARTIST QQpC", m));
  EXPECT_STREQ(m.text, "AB");
}

TEST(NowPlayingProtocol, EmptyTextClearsTheField) {
  Message m;
  ASSERT_TRUE(parseLine("NP ALBUM", m));
  EXPECT_EQ(m.type, MessageType::Album);
  EXPECT_STREQ(m.text, "");
}
