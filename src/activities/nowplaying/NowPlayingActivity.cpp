#include "NowPlayingActivity.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "SerialCommands.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "fontIds.h"
#include "util/TextFit.h"

using nowplaying::MessageType;
using nowplaying::PlayState;

namespace {
// Horizontal (400 x 300): the cover on the left, the text on the right, the progress bar below.
constexpr GfxRenderer::Orientation NOW_PLAYING_ORIENTATION = GfxRenderer::Orientation::LandscapeCounterClockwise;
constexpr int MARGIN = 12;
constexpr int ART_TOP = 12;
constexpr int TEXT_GAP = 18;                     // between the cover and the text column
constexpr unsigned long REDRAW_EVERY_MS = 5000;  // moves the progress bar while playing

void formatTime(uint32_t sec, char* out, size_t cap) {
  if (sec >= 3600) {
    snprintf(out, cap, "%u:%02u:%02u", static_cast<unsigned>(sec / 3600), static_cast<unsigned>(sec / 60 % 60),
             static_cast<unsigned>(sec % 60));
  } else {
    snprintf(out, cap, "%u:%02u", static_cast<unsigned>(sec / 60), static_cast<unsigned>(sec % 60));
  }
}
}  // namespace

NowPlayingActivity::NowPlayingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("NowPlaying", renderer, mappedInput) {}

NowPlayingActivity::~NowPlayingActivity() = default;

void NowPlayingActivity::onEnter() {
  Activity::onEnter();
  savedOrientation = renderer.getOrientation();
  renderer.setOrientation(NOW_PLAYING_ORIENTATION);
  // Two 8 KB cover buffers, held only while this screen is open (one shows, one receives).
  art = makeUniqueNoThrow<uint8_t[]>(nowplaying::kMaxArtBytes);
  incoming = makeUniqueNoThrow<uint8_t[]>(nowplaying::kMaxArtBytes);
  serialcmd::setHandler(&NowPlayingActivity::onSerial, this);
  char hello[24];
  snprintf(hello, sizeof(hello), "HELLO %d", ART_SIZE);  // asks the program to send everything, with covers this big
  sendEvent(hello);
  requestUpdate();
}

void NowPlayingActivity::onExit() {
  serialcmd::setHandler(nullptr, nullptr);
  renderer.setOrientation(savedOrientation);
  Activity::onExit();
  stopWifi();  // after the base class: this may restart the board
}

bool NowPlayingActivity::connected() const { return lastHostMs != 0 && millis() - lastHostMs < HOST_TIMEOUT_MS; }

void NowPlayingActivity::sendEvent(const char* name) {
  logSerial.printf("NP:%s\n", name);
  if (wifiActive && client.connected()) client.printf("NP:%s\n", name);
}

// ----- Wi-Fi -----------------------------------------------------------------------------------

void NowPlayingActivity::startWifi() {
  wifiAsked = true;
  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelected(true);
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelected(!result.isCancelled); });
}

void NowPlayingActivity::onWifiSelected(bool joined) {
  if (!joined || WiFi.status() != WL_CONNECTED) {
    wifiAsked = WiFi.getMode() != WIFI_MODE_NULL;  // WiFi may have been switched on even if the join failed
    requestUpdate();
    return;
  }
  // Light modem sleep: the radio sleeps between beacons, which saves power. Lines may arrive a little later.
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  server = makeUniqueNoThrow<WiFiServer>(nowplaying::kTcpPort);
  if (!server) {
    LOG_ERR("NOWPLAY", "out of memory: Wi-Fi server");
    requestUpdate();
    return;
  }
  server->begin();
  MDNS.end();
  if (!MDNS.begin(nowplaying::kHostName)) LOG_ERR("NOWPLAY", "mDNS failed to start");
  wifiActive = true;
  LOG_INF("NOWPLAY", "listening on %s:%u", WiFi.localIP().toString().c_str(),
          static_cast<unsigned>(nowplaying::kTcpPort));
  requestUpdate();
}

// WiFi leaves the heap fragmented, so (like the file-transfer screen) end the session with a restart to Home.
void NowPlayingActivity::stopWifi() {
  if (!wifiAsked) return;
  if (client) client.stop();
  server.reset();
  MDNS.end();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void NowPlayingActivity::pumpNetwork() {
  if (!wifiActive) return;
  if (!client || !client.connected()) {
    WiFiClient fresh = server->accept();
    if (!fresh) return;
    client.stop();
    client = fresh;
    client.setNoDelay(true);
    lineLen = 0;
    lineTooLong = false;
    char hello[24];
    snprintf(hello, sizeof(hello), "HELLO %d", ART_SIZE);
    sendEvent(hello);
  }
  int budget = 2048;  // bytes per pass, so a fast sender cannot starve the buttons
  while (budget-- > 0 && client.available() > 0) {
    const int ch = client.read();
    if (ch < 0) break;
    if (ch == '\n') {
      if (!lineTooLong) {
        lineBuf[lineLen] = '\0';
        handleNetworkLine(lineBuf);
      }
      lineLen = 0;
      lineTooLong = false;
    } else if (lineLen + 1 < LINE_BYTES) {
      lineBuf[lineLen++] = static_cast<char>(ch);
    } else {
      lineTooLong = true;  // drop the rest of an over-long line
    }
  }
}

// A network line has the same form as a serial one: "CMD:NP <verb> ...".
void NowPlayingActivity::handleNetworkLine(const char* line) {
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) --len;
  if (len < 7 || strncmp(line, "CMD:NP ", 7) != 0) return;
  lineBuf[len] = '\0';   // the trimmed line is still in lineBuf
  handleLine(line + 4);  // from "NP "
}

uint32_t NowPlayingActivity::displayPositionSec() const {
  uint32_t pos = basePositionSec;
  if (playState == PlayState::Playing) pos += static_cast<uint32_t>((millis() - positionStampMs) / 1000);
  return durationSec > 0 ? std::min(pos, durationSec) : pos;
}

// ----- Input from the computer -----------------------------------------------------------------

void NowPlayingActivity::onSerial(void* self, const char* command) {
  static_cast<NowPlayingActivity*>(self)->handleLine(command);
}

void NowPlayingActivity::handleLine(const char* line) {
  if (!nowplaying::parseLine(line, message)) return;
  lastHostMs = millis();
  bool changed;
  {
    std::lock_guard<std::mutex> guard(lock);
    apply(message);
    changed = message.type != MessageType::ArtData && message.type != MessageType::Ping;
  }
  if (message.type == MessageType::Ping) {
    sendEvent("PONG");
  } else if (message.type == MessageType::ArtData || message.type == MessageType::ArtBegin) {
    char ack[24];
    snprintf(ack, sizeof(ack), "ACK %u",
             static_cast<unsigned>(message.type == MessageType::ArtBegin ? 0 : message.offset));
    sendEvent(ack);
  }
  if (changed) requestUpdate();
}

// Applies one message to the shown state. Called with `lock` held.
void NowPlayingActivity::apply(const nowplaying::Message& m) {
  switch (m.type) {
    case MessageType::Title:
      snprintf(title, sizeof(title), "%s", m.text);
      break;
    case MessageType::Artist:
      snprintf(artist, sizeof(artist), "%s", m.text);
      break;
    case MessageType::Album:
      snprintf(album, sizeof(album), "%s", m.text);
      break;
    case MessageType::State:
      playState = m.state;
      basePositionSec = m.positionSec;
      durationSec = m.durationSec;
      positionStampMs = millis();
      break;
    case MessageType::ArtBegin:
      // A cover larger than the frame would be drawn outside it, so refuse it.
      receiving = incoming != nullptr && m.width <= ART_SIZE && m.height <= ART_SIZE;
      incomingWidth = m.width;
      incomingHeight = m.height;
      incomingTotal = m.totalBytes;
      incomingNext = 0;
      break;
    case MessageType::ArtData:
      // Chunks must arrive in order; a repeat of the chunk just stored (a lost ACK) is accepted again.
      if (receiving && m.offset == incomingNext && m.offset + m.dataLen <= incomingTotal) {
        memcpy(incoming.get() + m.offset, m.data, m.dataLen);
        incomingNext += static_cast<uint32_t>(m.dataLen);
      }
      break;
    case MessageType::ArtEnd:
      if (receiving && incomingNext == incomingTotal && art) {
        memcpy(art.get(), incoming.get(), incomingTotal);
        artWidth = incomingWidth;
        artHeight = incomingHeight;
        artValid = true;
      } else {
        artValid = false;  // an incomplete or refused cover must not leave the previous track's cover up
      }
      receiving = false;
      break;
    case MessageType::NoArt:
      artValid = false;
      receiving = false;
      break;
    case MessageType::Ping:
    case MessageType::None:
      break;
  }
}

// ----- Input from the board's keys -------------------------------------------------------------

// BOOT short = Up, KEY short = Down, hold BOOT = Back, hold KEY = Confirm.
void NowPlayingActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    onGoHome();
    return;
  }
  pumpNetwork();
  if (!connected() && !wifiAsked && mappedInput.wasReleased(Button::Confirm)) {
    startWifi();  // hold KEY on the waiting screen
    return;
  }
  if (mappedInput.wasReleased(Button::Up)) {
    sendEvent("PLAYPAUSE");
  } else if (mappedInput.wasReleased(Button::Down)) {
    sendEvent("NEXT");
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    sendEvent("PREV");
  }

  const bool nowConnected = connected();
  const unsigned long now = millis();
  if (nowConnected != wasConnected) {
    wasConnected = nowConnected;
    requestUpdate();
  } else if (nowConnected && playState == PlayState::Playing && now - lastRedrawMs >= REDRAW_EVERY_MS) {
    requestUpdate();
  }
}

// ----- Drawing ---------------------------------------------------------------------------------

void NowPlayingActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  char buf[nowplaying::kTextBytes + 8];

  if (!connected()) {
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 60, "Now Playing", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 - 20, "Waiting for the computer", true);
    // The full command to type, wrapped to the screen width.
    int y = h / 2 + 12;
    const auto drawWrapped = [&](const char* text) {
      for (const std::string& line : renderer.wrappedText(SMALL_FONT_ID, text, w - 2 * MARGIN, 2)) {
        renderer.drawCenteredText(SMALL_FONT_ID, y, line.c_str(), true);
        y += lineHeight + 2;
      }
    };
    if (wifiActive) {
      char info[96];
      snprintf(info, sizeof(info), "Wi-Fi: %s", WiFi.localIP().toString().c_str());
      drawWrapped(info);
      drawWrapped("Run in tools/nowplaying:");
      snprintf(info, sizeof(info), ".venv/bin/python nowplaying.py --host %s.local", nowplaying::kHostName);
      drawWrapped(info);
    } else {
      drawWrapped("USB cable, then run in tools/nowplaying:");
      drawWrapped(".venv/bin/python nowplaying.py");
      drawWrapped("Wi-Fi: hold the KEY button");
    }
  } else {
    std::lock_guard<std::mutex> guard(lock);
    // Cover art: 1 bit per pixel, 1 = black; with a thin frame, on the left.
    const int artX = MARGIN;
    renderer.drawRect(artX - 2, ART_TOP - 2, ART_SIZE + 4, ART_SIZE + 4, true);
    if (artValid && art) {
      const int x0 = artX + (ART_SIZE - artWidth) / 2;
      const int y0 = ART_TOP + (ART_SIZE - artHeight) / 2;
      const int rowBytes = (artWidth + 7) / 8;
      for (int y = 0; y < artHeight; ++y) {
        const uint8_t* row = art.get() + y * rowBytes;
        for (int bx = 0; bx < rowBytes; ++bx) {
          const uint8_t b = row[bx];
          if (b == 0) continue;  // all paper
          for (int bit = 0; bit < 8; ++bit) {
            const int x = bx * 8 + bit;
            if (x < artWidth && (b & (0x80 >> bit))) renderer.drawPixel(x0 + x, y0 + y, true);
          }
        }
      }
    } else {
      renderer.drawCenteredText(SMALL_FONT_ID, ART_TOP + ART_SIZE / 2 - lineHeight / 2, "No cover art", true);
    }

    // Text column to the right of the cover: title (up to 3 lines), artist (2), album (2).
    const int textX = artX + ART_SIZE + TEXT_GAP;
    const int textWidth = w - textX - MARGIN;
    int y = ART_TOP;
    const auto drawLines = [&](int fontId, EpdFontFamily::Style style, const char* text, int maxLines, int gapAfter) {
      for (const std::string& line : renderer.wrappedText(fontId, text, textWidth, maxLines, style)) {
        renderer.drawText(fontId, textX, y, line.c_str(), true, style);
        y += renderer.getLineHeight(fontId);
      }
      y += gapAfter;
    };
    // Regular weight: the bold UI fonts have no Korean glyphs (see convert-builtin-fonts.sh).
    drawLines(UI_12_FONT_ID, EpdFontFamily::REGULAR, title[0] ? title : "Nothing playing", 3, 8);
    if (artist[0]) drawLines(UI_10_FONT_ID, EpdFontFamily::REGULAR, artist, 2, 6);
    if (album[0]) drawLines(SMALL_FONT_ID, EpdFontFamily::REGULAR, album, 2, 0);

    // Progress bar across the bottom, with elapsed and total time and the play state.
    const int inner = w - 2 * MARGIN;
    const int barY = ART_TOP + ART_SIZE + 14;
    const uint32_t pos = displayPositionSec();
    renderer.drawRect(MARGIN, barY, inner, 8, 2, true);
    if (durationSec > 0)
      renderer.fillRect(MARGIN, barY, static_cast<int>(static_cast<uint64_t>(inner) * pos / durationSec), 8, true);
    formatTime(pos, buf, sizeof(buf));
    renderer.drawText(SMALL_FONT_ID, MARGIN, barY + 12, buf, true);
    formatTime(durationSec, buf, sizeof(buf));
    renderer.drawText(SMALL_FONT_ID, w - MARGIN - renderer.getTextWidth(SMALL_FONT_ID, buf), barY + 12, buf, true);
    const char* stateText = playState == PlayState::Playing  ? "Playing"
                            : playState == PlayState::Paused ? "Paused"
                                                             : "Stopped";
    renderer.drawCenteredText(SMALL_FONT_ID, barY + 12, stateText, true);
  }

  renderer.drawCenteredText(SMALL_FONT_ID, h - 2 * lineHeight - 6, "BOOT: play/pause   KEY: next", true);
  renderer.drawCenteredText(SMALL_FONT_ID, h - lineHeight - 4, "Hold KEY: previous   Hold BOOT: exit", true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  lastRedrawMs = millis();
}
