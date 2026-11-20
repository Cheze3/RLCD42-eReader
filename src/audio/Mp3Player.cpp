#include "Mp3Player.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cmath>

// An MP3 file on the SD card as a byte source for the decoder.
class SdSource final : public music::ByteSource {
 public:
  bool open(const std::string& path) {
    if (!Storage.openFileForRead("MUSIC", path, file)) return false;
    length = file.fileSize();
    return true;
  }
  // HalFile::close() asserts on a file that was never opened, so only close an open one.
  void close() {
    if (file.isOpen()) file.close();
  }
  int read(uint8_t* dst, size_t n) override { return file.read(dst, n); }
  bool seek(size_t pos) override { return file.seek(pos); }
  size_t size() const override { return length; }

 private:
  HalFile file;
  size_t length = 0;
};

namespace {
// The decoder's frame buffers need about 14 KB of stack on top of the call chain (estimated from the
// minimp3 source, not yet measured); the used amount is logged when the task ends.
constexpr uint32_t TASK_STACK_BYTES = 24 * 1024;
// After this long paused or finished, the codec and amplifier are switched off.
constexpr unsigned long SINK_IDLE_OFF_MS = 15000;
}  // namespace

Mp3Player::Mp3Player() = default;

// Callers must check end() first and leak the object when it returns false; a stuck worker still uses it.
Mp3Player::~Mp3Player() { end(); }

// Set when a worker could not be stopped; it still owns its player object, so no new player may start.
static std::atomic<bool> orphanedWorker{false};

bool Mp3Player::begin(AudioSink& audioSink) {
  if (task != nullptr) return true;
  if (orphanedWorker) return false;
  stream = makeUniqueNoThrow<music::Mp3Stream>();
  source = makeUniqueNoThrow<SdSource>();
  if (!stream || !source) {
    stream.reset();
    source.reset();
    return false;
  }
  sink = &audioSink;
  stream->setCancel(&stopRequested);
  stopRequested = false;
  taskDone = false;
  currentState = State::Idle;
  if (xTaskCreatePinnedToCore(&Mp3Player::taskEntry, "mp3_play", TASK_STACK_BYTES, this, 3, &task, 0) != pdPASS) {
    task = nullptr;
    taskDone = true;
    stream.reset();
    source.reset();
    return false;
  }
  return true;
}

bool Mp3Player::end() {
  if (task == nullptr) return true;
  stopRequested = true;  // the decoder also checks this, so even a long file scan stops promptly
  for (int i = 0; i < 300 && !taskDone; ++i) delay(10);
  if (!taskDone) {
    // Something (a stalled SD read) holds the worker. Freeing its buffers now would be a use after free,
    // so leave everything in place and tell the caller not to destroy this object.
    LOG_ERR("MP3", "player task did not stop in time");
    orphanedWorker = true;
    return false;
  }
  task = nullptr;
  if (sink != nullptr) sink->end();
  stream.reset();
  source.reset();
  sink = nullptr;
  return true;
}

void Mp3Player::taskEntry(void* self) {
  static_cast<Mp3Player*>(self)->run();
  vTaskDelete(nullptr);
}

void Mp3Player::play(const std::string& path, uint32_t startMs) {
  std::lock_guard<std::mutex> guard(lock);
  if (command == Command::Open) ++completed;  // the earlier open was replaced before the worker saw it
  command = Command::Open;
  commandPath = path;
  commandMs = startMs;
  ++requests;
}

void Mp3Player::pause() {
  if (currentState != State::Playing) return;
  idleSinceMs = millis();  // before the state changes, so the worker never sees Paused with an old time
  State expected = State::Playing;
  currentState.compare_exchange_strong(expected, State::Paused);
}

void Mp3Player::resume() {
  State expected = State::Paused;
  currentState.compare_exchange_strong(expected, State::Playing);
}

void Mp3Player::seekBy(int32_t deltaMs) {
  std::lock_guard<std::mutex> guard(lock);
  if (command == Command::None || command == Command::Seek) {
    command = Command::Seek;
    seekDeltaMs += deltaMs;
  }
}

void Mp3Player::stop() {
  std::lock_guard<std::mutex> guard(lock);
  if (command == Command::Open) ++completed;  // an open that never ran still counts as handled
  command = Command::Stop;
}

void Mp3Player::setVolume(uint8_t percent) {
  percent = std::min<uint8_t>(percent, 100);
  // 100% is full scale; each step down is about 0.45 dB, so 0% is -45 dB (effectively silent).
  gain = percent == 0 ? 0 : static_cast<int32_t>(256.0f * std::pow(10.0f, -(100 - percent) * 0.45f / 20.0f));
}

bool Mp3Player::trackTag(music::TrackInfo& out) const {
  std::lock_guard<std::mutex> guard(lock);
  if (!haveTag) return false;
  out = tag;
  return true;
}

void Mp3Player::closeTrack() {
  if (source) source->close();
  position = 0;
  duration = 0;
}

bool Mp3Player::openTrack(const std::string& path, uint32_t startMs) {
  closeTrack();
  currentState = State::Loading;
  if (!source->open(path) || !stream->open(*source)) {
    LOG_ERR("MP3", "cannot play %s", path.c_str());
    closeTrack();
    currentState = State::Failed;
    return false;
  }
  sampleRate = stream->sampleRate();
  if (!sink->setSampleRate(sampleRate)) {
    LOG_ERR("MP3", "audio output unavailable");
    closeTrack();
    currentState = State::Failed;
    return false;
  }
  if (startMs > 0) stream->seekMs(startMs);
  {
    std::lock_guard<std::mutex> guard(lock);
    tag = stream->tag();
    haveTag = true;
  }
  duration = stream->durationMs();
  position = stream->positionMs();
  ++trackGeneration;
  currentState = State::Playing;
  return true;
}

bool Mp3Player::feedSink(size_t frames) {
  const int32_t g = gain.load();
  if (g != 256) {
    for (size_t i = 0; i < frames * 2; ++i) pcm[i] = static_cast<int16_t>((pcm[i] * g) >> 8);
  }
  return sink->write(pcm, frames) > 0;
}

void Mp3Player::run() {
  while (!stopRequested) {
    Command cmd;
    std::string path;
    uint32_t startMs = 0;
    int32_t delta = 0;
    {
      std::lock_guard<std::mutex> guard(lock);
      cmd = command;
      path = std::move(commandPath);
      startMs = commandMs;
      delta = seekDeltaMs;
      command = Command::None;
      seekDeltaMs = 0;
    }

    if (cmd == Command::Open) {
      openTrack(path, startMs);
      ++completed;
    } else if (cmd == Command::Stop) {
      closeTrack();
      currentState = State::Idle;
      idleSinceMs = millis();
    } else if (cmd == Command::Seek && stream && duration.load() > 0) {
      const int64_t target = static_cast<int64_t>(stream->positionMs()) + delta;
      stream->seekMs(static_cast<uint32_t>(std::clamp<int64_t>(target, 0, duration.load())));
      position = stream->positionMs();
    }

    if (currentState == State::Playing) {
      // The sink may have been switched off while paused; this restarts it at the track's rate.
      if (!sink->active() && !sink->setSampleRate(sampleRate)) {
        currentState = State::Failed;
        continue;
      }
      const size_t frames = stream->readFrames(pcm);
      if (frames == 0) {
        closeTrack();
        currentState = State::Ended;
        idleSinceMs = millis();
      } else if (feedSink(frames)) {
        writeFailures = 0;
        position = stream->positionMs();
      } else {
        // The output took nothing (it has failed or been shut down). Do not decode at full speed into
        // the void: wait a little, and give up after a run of failures.
        vTaskDelay(pdMS_TO_TICKS(5));
        if (++writeFailures >= 20) {
          LOG_ERR("MP3", "audio output stopped accepting data");
          closeTrack();
          currentState = State::Failed;
          writeFailures = 0;
        }
      }
    } else {
      const bool quiet = currentState == State::Paused || currentState == State::Ended || currentState == State::Idle;
      if (sink->active() && quiet && millis() - idleSinceMs > SINK_IDLE_OFF_MS) sink->end();
      if (sink->active()) {
        // Keep the output fed with silence (the call paces itself on the hardware) so playback that
        // resumes starts in step with it instead of landing in a half-played buffer.
        std::fill(pcm, pcm + 512 * 2, static_cast<int16_t>(0));
        if (sink->write(pcm, 512) == 0) vTaskDelay(pdMS_TO_TICKS(10));
      } else {
        vTaskDelay(pdMS_TO_TICKS(20));
      }
    }
  }
  closeTrack();
  LOG_INF("MP3", "player task stack high water: %u bytes free",
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  taskDone = true;
}
