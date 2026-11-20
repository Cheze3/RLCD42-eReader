#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

// Music player state kept between sessions: where the user was, volume, play modes and the resume
// position of the last track.
class MusicStore : public PersistableStore<MusicStore> {
  MusicStore() = default;

  friend class PersistableStore<MusicStore>;

 public:
  std::string lastDir;   // folder last browsed
  std::string lastFile;  // full path of the last track played
  uint32_t positionSec = 0;
  uint8_t volume = 60;  // 0..100
  uint8_t shuffle = 0;
  uint8_t repeat = 0;  // 0 off, 1 all, 2 one

  static const char* getFilePath() { return "/.crosspoint/music.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
};

#define MUSIC_STORE MusicStore::getInstance()
