#include "ThermoStore.h"

#include <algorithm>

void ThermoStore::toJson(JsonDocument& doc) const { doc["offsetC"] = offsetC; }

bool ThermoStore::fromJson(JsonVariantConst doc) {
  // A wild value in a hand-edited file would wreck every reading, so keep it within +-10 C.
  offsetC = std::clamp(doc["offsetC"] | 0.0f, -10.0f, 10.0f);
  return true;
}
