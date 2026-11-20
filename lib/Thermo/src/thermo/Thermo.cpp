#include "Thermo.h"

#include <algorithm>
#include <cmath>

namespace thermo {

namespace {
constexpr float kMagnusA = 17.62f;
constexpr float kMagnusB = 243.12f;  // degrees C
}  // namespace

uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
  }
  return crc;
}

bool decodeMeasurement(const uint8_t raw[6], float& tempC, float& rhPercent) {
  if (crc8(raw, 2) != raw[2] || crc8(raw + 3, 2) != raw[5]) return false;
  const uint16_t t = static_cast<uint16_t>(raw[0] << 8 | raw[1]);
  const uint16_t h = static_cast<uint16_t>(raw[3] << 8 | raw[4]);
  tempC = -45.0f + 175.0f * static_cast<float>(t) / 65536.0f;
  rhPercent = std::clamp(100.0f * static_cast<float>(h) / 65536.0f, 0.0f, 100.0f);
  return true;
}

bool isShtc3Id(const uint8_t raw[3]) {
  if (crc8(raw, 2) != raw[2]) return false;
  const uint16_t id = static_cast<uint16_t>(raw[0] << 8 | raw[1]);
  return (id & 0x083F) == 0x0807;
}

float saturationPressureHpa(float tempC) { return 6.112f * std::exp(kMagnusA * tempC / (kMagnusB + tempC)); }

float dewPointC(float tempC, float rhPercent) {
  const float rh = std::max(rhPercent, 0.1f);
  const float gamma = std::log(rh / 100.0f) + kMagnusA * tempC / (kMagnusB + tempC);
  return kMagnusB * gamma / (kMagnusA - gamma);
}

float ambientHumidity(float rhMeasured, float tempMeasured, float tempAmbient) {
  return std::clamp(rhMeasured * saturationPressureHpa(tempMeasured) / saturationPressureHpa(tempAmbient), 0.0f,
                    100.0f);
}

void Averager::add(float tempC, float rhPercent) {
  const int slot = total % kWindow;
  temps[slot] = tempC;
  rhs[slot] = rhPercent;
  ++total;
}

namespace {
float rangeOf(const float* values, int n) {
  if (n == 0) return 0;
  const auto [lo, hi] = std::minmax_element(values, values + n);
  return *hi - *lo;
}
float meanOf(const float* values, int n) {
  if (n == 0) return 0;
  float sum = 0;
  for (int i = 0; i < n; ++i) sum += values[i];
  return sum / static_cast<float>(n);
}
}  // namespace

bool Averager::settled() const { return count() == kWindow && tempRange() <= kSettledTempC && rhRange() <= kSettledRh; }
float Averager::meanTemp() const { return meanOf(temps, count()); }
float Averager::meanRh() const { return meanOf(rhs, count()); }
float Averager::tempRange() const { return rangeOf(temps, count()); }
float Averager::rhRange() const { return rangeOf(rhs, count()); }

}  // namespace thermo
