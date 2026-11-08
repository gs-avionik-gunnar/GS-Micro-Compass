// GY-271 magnetometer driver (QMC5883L at 0x0D or HMC5883L at 0x1E, detected automatically). No library: plain I2C.
#pragma once
#include <stdint.h>

#include "Compass.h"

namespace mag {
void i2cBegin();                      // starts the I2C bus on the Grove port
bool detect();                        // probes and initialises the sensor; false if none answers
compass::SensorType type();
// 1 = new sample, 0 = no new data yet, -1 = bus error (sensor removed?)
int read(int16_t& x, int16_t& y, int16_t& z);
// Typical horizontal field range seen when turning the unit once around (raw counts), for the progress bars.
int expectedSpan();
}  // namespace mag
