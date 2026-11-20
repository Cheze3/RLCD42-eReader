#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

// Thermometer calibration. The board's own heat makes the sensor read warmer than the room; the
// offset is added to the measured temperature (so a negative value cools it) and is used to
// correct the humidity too. Stored in /.crosspoint/thermo.json; 0 means uncalibrated.
class ThermoStore : public PersistableStore<ThermoStore> {
  ThermoStore() = default;

  friend class PersistableStore<ThermoStore>;

 public:
  float offsetC = 0.0f;

  static const char* getFilePath() { return "/.crosspoint/thermo.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
};

#define THERMO_STORE ThermoStore::getInstance()
