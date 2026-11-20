#include <gtest/gtest.h>
#include <thermo/Thermo.h>

using namespace thermo;

namespace {
// Builds a valid 6-byte frame from raw temperature and humidity words.
void frame(uint16_t t, uint16_t h, uint8_t out[6]) {
  out[0] = t >> 8;
  out[1] = t & 0xFF;
  out[2] = crc8(out, 2);
  out[3] = h >> 8;
  out[4] = h & 0xFF;
  out[5] = crc8(out + 3, 2);
}
}  // namespace

TEST(Thermo, CrcMatchesTheSensirionDatasheetExample) {
  const uint8_t data[] = {0xBE, 0xEF};
  EXPECT_EQ(crc8(data, 2), 0x92);
}

TEST(Thermo, DecodesTemperatureAndHumidity) {
  uint8_t raw[6];
  frame(0x6666, 0x8000, raw);  // 0x6666/65536 = 0.4 -> -45 + 70 = 25 C; 0x8000 = 50 %
  float t = 0, h = 0;
  ASSERT_TRUE(decodeMeasurement(raw, t, h));
  EXPECT_NEAR(t, 25.0f, 0.01f);
  EXPECT_NEAR(h, 50.0f, 0.01f);
}

TEST(Thermo, RejectsABadCrcInEitherHalf) {
  uint8_t raw[6];
  frame(0x6666, 0x8000, raw);
  float t, h;
  raw[2] ^= 1;
  EXPECT_FALSE(decodeMeasurement(raw, t, h));
  frame(0x6666, 0x8000, raw);
  raw[5] ^= 1;
  EXPECT_FALSE(decodeMeasurement(raw, t, h));
}

TEST(Thermo, RecognisesTheShtc3Id) {
  uint8_t id[3] = {0x08, 0x07, 0};
  id[2] = crc8(id, 2);
  EXPECT_TRUE(isShtc3Id(id));
  uint8_t other[3] = {0x08, 0x06, 0};  // right family bits, wrong low pattern: not an SHTC3
  other[2] = crc8(other, 2);
  EXPECT_FALSE(isShtc3Id(other));
  id[2] ^= 1;
  EXPECT_FALSE(isShtc3Id(id));
}

TEST(Thermo, DewPointMatchesReferenceValues) {
  EXPECT_NEAR(dewPointC(25.0f, 50.0f), 13.86f, 0.1f);
  EXPECT_NEAR(dewPointC(20.0f, 100.0f), 20.0f, 0.05f);
  EXPECT_NEAR(dewPointC(30.0f, 70.0f), 24.0f, 0.2f);
}

TEST(Thermo, WarmSensorHumidityIsCorrectedUpToTheRoom) {
  // Air at 24 C and 45 % that is warmed to 26 C at the sensor keeps its vapour pressure, so the
  // sensor reads a lower humidity (about 40 %).
  const float measured = 45.0f * saturationPressureHpa(24.0f) / saturationPressureHpa(26.0f);
  EXPECT_LT(measured, 45.0f);
  EXPECT_NEAR(ambientHumidity(measured, 26.0f, 24.0f), 45.0f, 0.05f);
  EXPECT_NEAR(ambientHumidity(40.0f, 25.0f, 25.0f), 40.0f, 0.001f);  // no offset, no change
  EXPECT_LE(ambientHumidity(99.0f, 30.0f, 20.0f), 100.0f);
}

TEST(Thermo, AveragerNeedsAFullWindowToSettle) {
  Averager a;
  for (int i = 0; i < Averager::kWindow - 1; ++i) a.add(24.0f, 50.0f);
  EXPECT_FALSE(a.settled());
  a.add(24.0f, 50.0f);
  EXPECT_TRUE(a.settled());
  EXPECT_NEAR(a.meanTemp(), 24.0f, 1e-4f);
  EXPECT_NEAR(a.meanRh(), 50.0f, 1e-4f);
}

TEST(Thermo, AveragerDoesNotSettleWhileDrifting) {
  Averager a;
  for (int i = 0; i < 40; ++i) a.add(24.0f + 0.02f * i, 50.0f);  // 0.4 C over the last 20 readings
  EXPECT_FALSE(a.settled());
  EXPECT_NEAR(a.tempRange(), 0.38f, 0.01f);
}

TEST(Thermo, AveragerKeepsOnlyTheLatestWindow) {
  Averager a;
  for (int i = 0; i < 100; ++i) a.add(10.0f, 30.0f);  // old, different readings
  for (int i = 0; i < Averager::kWindow; ++i) a.add(24.0f, 50.0f);
  EXPECT_TRUE(a.settled());
  EXPECT_NEAR(a.meanTemp(), 24.0f, 1e-4f);
}

TEST(Thermo, AveragerToleratesSensorNoise) {
  Averager a;
  for (int i = 0; i < Averager::kWindow; ++i) a.add(24.0f + ((i % 3) - 1) * 0.03f, 50.0f + ((i % 2) ? 0.2f : -0.2f));
  EXPECT_TRUE(a.settled());
}
