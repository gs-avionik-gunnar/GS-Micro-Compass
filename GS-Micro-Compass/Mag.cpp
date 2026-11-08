#include "Mag.h"

#include <Arduino.h>
#include <Wire.h>

#include "Config.h"

namespace mag {

namespace {
const uint8_t kAddrQmc = 0x0D, kAddrHmc = 0x1E;
compass::SensorType s_type = compass::kSensorNone;

bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, int n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if ((int)Wire.requestFrom((int)addr, n) != n) return false;
  for (int i = 0; i < n; ++i) buf[i] = (uint8_t)Wire.read();
  return true;
}
}  // namespace

void i2cBegin() { Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ); }

bool detect() {
  s_type = compass::kSensorNone;
  if (probe(kAddrQmc)) {
    writeReg(kAddrQmc, 0x0B, 0x01);                 // SET/RESET period
    // OSR 512, range 8 G, 200 Hz, continuous
    if (writeReg(kAddrQmc, 0x09, 0x1D)) s_type = compass::kSensorQmc;
  } else if (probe(kAddrHmc)) {
    writeReg(kAddrHmc, 0x00, 0x78);                 // 8x averaging, 75 Hz
    writeReg(kAddrHmc, 0x01, 0x20);                 // gain +-1.3 Ga
    if (writeReg(kAddrHmc, 0x02, 0x00)) s_type = compass::kSensorHmc;  // continuous
  }
  return s_type != compass::kSensorNone;
}

compass::SensorType type() { return s_type; }

int read(int16_t& x, int16_t& y, int16_t& z) {
  uint8_t b[6];
  if (s_type == compass::kSensorQmc) {
    uint8_t st;
    if (!readRegs(kAddrQmc, 0x06, &st, 1)) return -1;
    if (!(st & 0x01)) return 0;  // DRDY
    if (!readRegs(kAddrQmc, 0x00, b, 6)) return -1;
    x = (int16_t)((b[1] << 8) | b[0]);
    y = (int16_t)((b[3] << 8) | b[2]);
    z = (int16_t)((b[5] << 8) | b[4]);
    return 1;
  }
  if (s_type == compass::kSensorHmc) {
    if (!readRegs(kAddrHmc, 0x03, b, 6)) return -1;
    x = (int16_t)((b[0] << 8) | b[1]);  // order X, Z, Y
    z = (int16_t)((b[2] << 8) | b[3]);
    y = (int16_t)((b[4] << 8) | b[5]);
    if (x == -4096 || y == -4096 || z == -4096) return 0;  // overflow
    return 1;
  }
  return -1;
}

int expectedSpan() { return s_type == compass::kSensorHmc ? 350 : 1000; }

}  // namespace mag
