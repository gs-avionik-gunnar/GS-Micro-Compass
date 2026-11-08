// GS-Micro-Compass 2.1 - magnetic compass with three instrument styles (ESP32-S3, round display, wheel).
//
// Architecture (see README.md):
//   Compass.*     heading, filter, calibration, shared state       (pure C++, unit-tested)
//   Mag.*         GY-271 driver (QMC5883L / HMC5883L, plain I2C)
//   Settings*.*   NVS persistence, defaults and limits, migration from 1.x
//   WebUi.*       access point + HTTP UI + OTA (own FreeRTOS task, only while WiFi is on)
//   Gfx/DialUi/DialHw/DialFont/BootAnim  round display, wheel, buzzer, boot logo animation
//
// Build with the Arduino IDE: board "ESP32S3 Dev Module", Arduino-ESP32 core 2.0.x or 3.x.
// No additional libraries are needed.

#include <Arduino.h>

#include "App.h"
#include "Compass.h"
#include "Config.h"
#include "DialHw.h"
#include "Mag.h"
#include "Settings.h"
#include "WebUi.h"

static const uint32_t kCalDurationMs = 20000;

static compass::Heading g_heading;
static compass::Calibrator g_calib;
static compass::State st;  // working copy owned by loop(); published as a snapshot
static uint32_t calStartMs = 0;
static int busErrors = 0;

static void configureHeading() {
  compass::Mount m{g_settings.swapXY, g_settings.invX, g_settings.invY};
  g_heading.configure(m, g_settings.smooth);
  g_heading.setCal(g_cal);
}

static void finishCalibration() {
  compass::Cal c;
  if (g_calib.finish(c)) {
    g_cal = c;
    calSave(c);
    st.calibrated = true;
    st.calResult = 1;
  } else {
    st.calResult = 2;
  }
  st.calActive = false;
  configureHeading();
  g_heading.reset();
}

static void startCalibration(uint32_t now) {
  st.calResult = 0;
  if (st.sensor == compass::kSensorNone) {
    st.calResult = 2;  // no sensor: the display shows "Failed"
    return;
  }
  g_calib.begin();
  st.calActive = true;
  st.calRemainingMs = kCalDurationMs;
  st.calProgX = st.calProgY = 0;
  st.spanX = st.spanY = 0;
  calStartMs = now;
}

static void handleCalCommand(uint8_t cmd, uint32_t now) {
  if (cmd == kCalStart) {
    startCalibration(now);
  } else if (cmd == kCalStop && st.calActive) {
    finishCalibration();
  } else if (cmd == kCalCancel && st.calActive) {
    st.calActive = false;  // keep the old calibration
    st.calResult = 0;
    g_heading.reset();
  }
}

static uint8_t progress(float span) {
  int p = (int)(span * 100.0f / (float)mag::expectedSpan());
  return (uint8_t)(p < 0 ? 0 : (p > 100 ? 100 : p));
}

void setup() {
  Serial.begin(115200);
  appInit();
  settingsLoad(g_settings);
  calLoad(g_cal);
  mag::i2cBegin();
  if (mag::detect()) st.sensor = mag::type();
  st.calibrated = g_cal.valid;
  configureHeading();

  if (dialhw::supported()) dialhw::begin();
  else Serial.println("This firmware needs the ESP32-S3 board (select \"ESP32S3 Dev Module\")");
  webBegin();
  if (g_settings.wifiBoot) webStart();
  Serial.printf("\n%s %s, built %s %s, sensor %s\n", g_hwName, GS_FW_VERSION, __DATE__, __TIME__,
                compass::sensorName(st.sensor));
}

void loop() {
  static uint32_t lastProbe = 0, lastPublish = 0, lastDebug = 0;
  const uint32_t now = millis();

  const uint8_t cmd = g_calCmd;
  if (cmd) {
    g_calCmd = kCalNone;
    handleCalCommand(cmd, now);
  }

  if (st.calActive) {
    const uint32_t el = now - calStartMs;
    if (el >= kCalDurationMs) {
      finishCalibration();
    } else {
      st.calRemainingMs = kCalDurationMs - el;
    }
  }

  if (st.sensor == compass::kSensorNone) {
    if (now - lastProbe >= 1000) {  // sensor missing or unplugged: keep looking for it
      lastProbe = now;
      if (mag::detect()) {
        st.sensor = mag::type();
        g_heading.reset();
      }
    }
  } else {
    int16_t x, y, z;
    const int r = mag::read(x, y, z);
    if (r == 1) {
      busErrors = 0;
      st.rawX = x;
      st.rawY = y;
      st.rawZ = z;
      st.lastRxMs = now;
      if (st.calActive) {
        g_calib.feed(x, y);
        st.spanX = g_calib.spanX();
        st.spanY = g_calib.spanY();
        st.calProgX = progress(st.spanX);
        st.calProgY = progress(st.spanY);
      } else {
        g_heading.feed(x, y);
        st.rawDeg = g_heading.rawDeg();
      }
    } else if (r < 0 && ++busErrors > 50) {
      busErrors = 0;
      st.sensor = compass::kSensorNone;
      if (st.calActive) finishCalibration();
    }
  }

  if (now - lastPublish >= 20) {
    lastPublish = now;
    statePublish(st);
  }

  if (g_settings.debug && now - lastDebug >= (g_settings.debug >= 2 ? 100u : 500u)) {
    lastDebug = now;
    const float h = compass::applyOffsets(st.rawDeg, g_settings.declTenths, g_settings.offsetDeg);
    if (g_settings.debug >= 2) Serial.printf("%d,%d,%d,%.1f\n", st.rawX, st.rawY, st.rawZ, (double)h);  // serial plotter
    else Serial.printf("heading %.1f raw %.1f x %d y %d z %d\n", (double)h, (double)st.rawDeg, st.rawX, st.rawY, st.rawZ);
  }
  delay(2);  // yields to the display and WiFi tasks
}
