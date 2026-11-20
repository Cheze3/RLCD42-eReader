#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>

// High scores (one per difficulty = number of notes) and the last menu choices of the Note Quiz.
class NoteQuizStore : public PersistableStore<NoteQuizStore> {
  NoteQuizStore() = default;

  friend class PersistableStore<NoteQuizStore>;

 public:
  static constexpr int LEVELS = 4;

  uint16_t best[LEVELS] = {};  // index = notes - 1
  uint8_t lastNotes = 1;       // 1..4
  uint8_t lastRandomMode = 0;  // 0 = chord, 1 = random notes

  static const char* getFilePath() { return "/.crosspoint/notequiz.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
};

#define NOTE_QUIZ_STORE NoteQuizStore::getInstance()
