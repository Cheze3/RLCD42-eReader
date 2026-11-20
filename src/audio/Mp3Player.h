#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <music/Mp3Stream.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "AudioSink.h"

// Plays MP3 files from the SD card through an AudioSink. A worker task on core 0 reads, decodes and
// feeds the sink; the calling (UI) task only sends commands and reads the status.
class Mp3Player {
 public:
  enum class State : uint8_t { Idle, Loading, Playing, Paused, Ended, Failed };

  Mp3Player();
  ~Mp3Player();  // calls end()

  // Allocates the decoder (about 30 KB) and starts the worker. Returns false when out of memory.
  bool begin(AudioSink& sink);
  // Stops the worker and releases everything. Returns false when the worker did not stop in time (for
  // example a stalled SD card): nothing is freed then, and the caller must not destroy this object, since
  // the worker may still be using it.
  bool end();

  // Loads `path` and starts playing at `startMs`.
  void play(const std::string& path, uint32_t startMs = 0);
  void pause();
  void resume();
  void seekBy(int32_t deltaMs);
  void stop();
  void setVolume(uint8_t percent);

  State state() const { return currentState.load(); }
  bool playing() const { return state() == State::Playing || state() == State::Loading; }
  uint32_t positionMs() const { return position.load(); }
  uint32_t durationMs() const { return duration.load(); }
  // Increments each time a new track has been opened, so the UI knows to refresh the title.
  uint32_t generation() const { return trackGeneration.load(); }
  // play() calls made, and how many the worker has finished handling (opened or failed). Equal means
  // no open is pending, so an Ended or Failed state is final.
  uint32_t openRequests() const { return requests.load(); }
  uint32_t openCompleted() const { return completed.load(); }
  // Copy of the current track's tag. False until the first track is open.
  bool trackTag(music::TrackInfo& out) const;

 private:
  enum class Command : uint8_t { None, Open, Seek, Stop };

  static void taskEntry(void* self);
  void run();
  bool openTrack(const std::string& path, uint32_t startMs);
  void closeTrack();
  bool feedSink(size_t frames);

  AudioSink* sink = nullptr;
  TaskHandle_t task = nullptr;
  std::unique_ptr<music::Mp3Stream> stream;
  std::unique_ptr<class SdSource> source;
  int16_t pcm[music::Mp3Stream::kMaxFrames * 2] = {};

  mutable std::mutex lock;  // guards the command fields and the copied tag
  Command command = Command::None;
  std::string commandPath;
  uint32_t commandMs = 0;
  int32_t seekDeltaMs = 0;
  music::TrackInfo tag;
  bool haveTag = false;

  std::atomic<State> currentState{State::Idle};
  std::atomic<uint32_t> position{0};
  std::atomic<uint32_t> duration{0};
  std::atomic<uint32_t> trackGeneration{0};
  std::atomic<uint32_t> requests{0};
  std::atomic<uint32_t> completed{0};
  std::atomic<int32_t> gain{256};  // 8.8 fixed point, 256 = unity
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> taskDone{true};
  uint32_t sampleRate = 0;
  std::atomic<unsigned long> idleSinceMs{0};  // set by pause() and by the worker; read by the worker
  int writeFailures = 0;                      // worker only: consecutive sink writes that accepted nothing
};
