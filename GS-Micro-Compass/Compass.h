// Compass logic: heading from the two horizontal magnetometer axes, calibration, shared state.
// Pure C++ (no Arduino dependencies) so it can be unit-tested on a PC.
#pragma once
#include <stdint.h>

namespace compass {

// Minimum measured range (raw counts) on each axis for a plausible calibration.
constexpr float kMinSpan = 80.0f;

// Hard-iron offset and soft-iron scale of the two horizontal axes (raw sensor axes).
struct Cal {
  float offX, offY, scX, scY;
  bool valid;
};
inline Cal defaultCal() { return Cal{0.0f, 0.0f, 1.0f, 1.0f, false}; }

// How the sensor board is mounted relative to the display (applied after the calibration).
struct Mount {
  bool swapXY, invX, invY;
};

float wrap360(float deg);  // -> [0, 360)

// Heading shown to the user: sensor angle + magnetic declination (tenths of a degree) + offset (degrees).
float applyOffsets(float rawDeg, int declTenths, int offsetDeg);

// Low-pass filtered heading. The filter runs on the field vector, so there is no jump at 359 -> 0.
class Heading {
 public:
  void configure(const Mount& m, int smoothPct);  // smoothPct 5..100 (100 = no smoothing)
  void setCal(const Cal& c) { cal_ = c; }
  void reset() { init_ = false; }
  void feed(int16_t rawX, int16_t rawY);
  bool hasData() const { return init_; }
  float rawDeg() const;  // 0..360, sensor frame, without declination and offset

 private:
  Mount m_{false, false, false};
  float alpha_ = 0.15f;
  Cal cal_ = defaultCal();
  float fx_ = 0.0f, fy_ = 0.0f;
  bool init_ = false;
};

// Min/max calibration while the unit is turned in all directions (level).
class Calibrator {
 public:
  void begin();
  void feed(int16_t rawX, int16_t rawY);
  float spanX() const { return n_ ? maxX_ - minX_ : 0.0f; }
  float spanY() const { return n_ ? maxY_ - minY_ : 0.0f; }
  int samples() const { return n_; }
  // true when the data is plausible; `out` is only filled in that case
  bool finish(Cal& out) const;

 private:
  float minX_ = 0, maxX_ = 0, minY_ = 0, maxY_ = 0;
  int n_ = 0;
};

// ---- snapshot shared between loop() (owner), the display task and the web task -----------------------------------
enum SensorType : uint8_t { kSensorNone = 0, kSensorQmc = 1, kSensorHmc = 2 };
inline const char* sensorName(uint8_t t) { return t == kSensorQmc ? "QMC5883L" : (t == kSensorHmc ? "HMC5883L" : "none"); }

struct State {
  float rawDeg = 0.0f;          // sensor frame, filtered
  uint32_t lastRxMs = 0;        // time of the last sensor sample (0 = none yet)
  uint8_t sensor = kSensorNone;
  int16_t rawX = 0, rawY = 0, rawZ = 0;
  bool calibrated = false;      // a calibration is stored
  bool calActive = false;       // calibration running
  uint32_t calRemainingMs = 0;
  uint8_t calProgX = 0, calProgY = 0;  // 0..100, how much of the expected range has been covered
  uint8_t calResult = 0;        // 0 none, 1 stored, 2 failed (valid after calActive went false)
  float spanX = 0, spanY = 0;   // range measured in the last / running calibration

  // Age of a time stamp. A stamp slightly newer than `now` (sample taken after the caller read the clock) counts as age 0
  // instead of wrapping around to a huge value.
  static uint32_t age(uint32_t now, uint32_t t) {
    int32_t d = (int32_t)(now - t);
    return d < 0 ? 0u : (uint32_t)d;
  }
  bool fresh(uint32_t now) const { return lastRxMs != 0 && age(now, lastRxMs) < 1000; }
};

}  // namespace compass
