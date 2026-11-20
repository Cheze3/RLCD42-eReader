#include "MusicStore.h"

void MusicStore::toJson(JsonDocument& doc) const {
  doc["lastDir"] = lastDir;
  doc["lastFile"] = lastFile;
  doc["positionSec"] = positionSec;
  doc["volume"] = volume;
  doc["shuffle"] = shuffle;
  doc["repeat"] = repeat;
}

bool MusicStore::fromJson(JsonVariantConst doc) {
  lastDir = doc["lastDir"] | "";
  lastFile = doc["lastFile"] | "";
  positionSec = doc["positionSec"] | static_cast<uint32_t>(0);
  const uint8_t vol = doc["volume"] | static_cast<uint8_t>(60);
  volume = vol <= 100 ? vol : 60;
  shuffle = (doc["shuffle"] | static_cast<uint8_t>(0)) ? 1 : 0;
  const uint8_t rep = doc["repeat"] | static_cast<uint8_t>(0);
  repeat = rep <= 2 ? rep : 0;
  return true;
}
