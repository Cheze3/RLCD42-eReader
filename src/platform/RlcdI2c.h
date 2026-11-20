#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <esp32-hal-i2c.h>

// The RLCD-4.2 board's one I2C bus (SDA 13, SCL 14): the clock chip, motion sensor, SHTC3 and the two audio
// codecs all share it. The SDK's clock and motion-sensor drivers start it once at boot and remember
// that, so no activity may stop it: never call Wire.end(). Activities that talk to a chip on it call
// ensureI2c() first, which starts the bus only if nothing has yet.
namespace rlcd {

constexpr int kI2cSda = 13;
constexpr int kI2cScl = 14;
constexpr uint32_t kI2cHz = 400000;  // the speed the SDK uses for this bus

inline void ensureI2c() {
  if (!i2cIsInit(0)) Wire.begin(kI2cSda, kI2cScl, kI2cHz);
}

}  // namespace rlcd
