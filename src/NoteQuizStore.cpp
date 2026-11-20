#include "NoteQuizStore.h"

void NoteQuizStore::toJson(JsonDocument& doc) const {
  JsonArray scores = doc["best"].to<JsonArray>();
  for (const uint16_t score : best) scores.add(score);
  doc["lastNotes"] = lastNotes;
  doc["lastRandomMode"] = lastRandomMode;
}

bool NoteQuizStore::fromJson(JsonVariantConst doc) {
  JsonArrayConst scores = doc["best"];
  for (int i = 0; i < LEVELS; ++i) best[i] = scores[i] | static_cast<uint16_t>(0);
  const uint8_t notes = doc["lastNotes"] | static_cast<uint8_t>(1);
  lastNotes = notes >= 1 && notes <= LEVELS ? notes : 1;
  lastRandomMode = (doc["lastRandomMode"] | static_cast<uint8_t>(0)) ? 1 : 0;
  return true;
}
