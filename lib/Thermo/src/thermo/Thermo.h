#pragma once

#include <cstddef>
#include <cstdint>

// Temperature and humidity maths for the Sensirion SHTC3 on the RLCD-4.2 board. Pure functions and no
// hardware access, so it also builds in the host tests.
namespace thermo {

// Sensirion CRC-8 (polynomial 0x31, initial value 0xFF) over `len` bytes.
uint8_t crc8(const uint8_t* data, size_t len);

// Decodes one 6-byte SHTC3 reading in temperature-first order (T msb, T lsb, CRC, RH msb, RH lsb,
// CRC). Returns false when either CRC fails.
bool decodeMeasurement(const uint8_t raw[6], float& tempC, float& rhPercent);

// True when the 3-byte ID read (msb, lsb, CRC) is an SHTC3.
bool isShtc3Id(const uint8_t raw[3]);

// Magnus-formula saturation vapour pressure in hPa and dew point in degrees C.
float saturationPressureHpa(float tempC);
float dewPointC(float tempC, float rhPercent);

// The board warms the sensor, so the air next to it is warmer than the room. The vapour pressure
// is the same in both places, so the room's relative humidity is the measured value scaled by the
// ratio of the two saturation pressures. Clamped to 0..100.
float ambientHumidity(float rhMeasured, float tempMeasured, float tempAmbient);

// The most recent kWindow readings, for averaging and for telling when they have stopped drifting.
class Averager {
 public:
  static constexpr int kWindow = 20;             // 10 s at two readings per second
  static constexpr float kSettledTempC = 0.12f;  // max spread of the window to call it settled
  static constexpr float kSettledRh = 1.0f;

  void reset() { total = 0; }
  void add(float tempC, float rhPercent);
  int count() const { return total < kWindow ? total : kWindow; }
  // A full window whose temperature and humidity each vary by no more than the limits above.
  bool settled() const;
  float meanTemp() const;
  float meanRh() const;
  float tempRange() const;
  float rhRange() const;

 private:
  float temps[kWindow] = {};
  float rhs[kWindow] = {};
  int total = 0;  // readings added since reset; the window holds the latest min(total, kWindow)
};

}  // namespace thermo
