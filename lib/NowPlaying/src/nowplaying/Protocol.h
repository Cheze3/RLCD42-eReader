#pragma once

#include <cstddef>
#include <cstdint>

// The line protocol between the "now playing" screen and the program on the computer.
//
// Computer to board, one line each, sent over the USB serial port as `CMD:NP <verb> ...` (the board's
// serial-command prefix). parseLine() takes the line from `NP` onwards:
//   NP TITLE <base64 UTF-8>          NP ARTIST <base64>        NP ALBUM <base64>
//   NP STATE <playing|paused|stopped> <positionSec> <durationSec>
//   NP ART <width> <height> <totalBytes>      then   NP ARTDATA <offset> <base64 bytes> ...   NP ARTEND
//   NP NOART                          NP PING
// The board accepts art chunks of up to kMaxChunkBytes, but the computer program keeps every line under about
// 220 characters, so a line fits even the USB serial driver's default 256-byte buffer.
// Art is a 1-bit image, rows of ceil(width/8) bytes, most significant bit first, 1 = black.
// Board to computer: `NP:HELLO <artSize>` (screen opened; send a square cover of that many pixels),
// `NP:ACK <offset>` (art chunk stored),
// `NP:PONG`, and the controls `NP:PLAYPAUSE`, `NP:NEXT`, `NP:PREV`.
// Over Wi-Fi the same lines travel on a TCP connection to kTcpPort (the board is "kHostName.local").
namespace nowplaying {

constexpr uint16_t kTcpPort = 8265;
constexpr const char* kHostName = "crosspoint-np";

constexpr size_t kMaxArtBytes = 8192;   // 256 x 256 at 1 bit
constexpr size_t kMaxChunkBytes = 768;  // decoded bytes in one ARTDATA line
constexpr size_t kTextBytes = 96;

enum class MessageType : uint8_t { None, Title, Artist, Album, State, ArtBegin, ArtData, ArtEnd, NoArt, Ping };
enum class PlayState : uint8_t { Stopped, Playing, Paused };

struct Message {
  MessageType type = MessageType::None;
  char text[kTextBytes] = "";  // Title, Artist, Album: UTF-8, cut on a character boundary
  PlayState state = PlayState::Stopped;
  uint32_t positionSec = 0;
  uint32_t durationSec = 0;
  uint16_t width = 0;  // ArtBegin
  uint16_t height = 0;
  uint32_t totalBytes = 0;
  uint32_t offset = 0;  // ArtData
  uint8_t data[kMaxChunkBytes] = {};
  size_t dataLen = 0;
};

// Parses one protocol line. Returns false for anything malformed, unknown or out of limits.
bool parseLine(const char* line, Message& out);

// Decodes standard base64 (with padding). Returns the byte count, or SIZE_MAX on bad input or when
// `out` is too small.
size_t base64Decode(const char* in, size_t inLen, uint8_t* out, size_t outCap);

}  // namespace nowplaying
