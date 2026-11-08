// Shared runtime state. Threading model:
//   * loop() task  : owns the sensor, the heading and the calibration; publishes a snapshot under g_stateMutex
//   * display task : reads the snapshot (stateSnapshot), asks loop() for a calibration through g_calCmd
//   * web task     : reads the snapshot; edits a copy of the settings, saves and reboots
//   * g_settings   : read-only at runtime except for the few values the display menu changes (DialHw copies them)
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "Compass.h"
#include "Settings.h"

enum CalCmd : uint8_t { kCalNone = 0, kCalStart, kCalStop, kCalCancel };

extern Settings g_settings;
extern compass::State g_state;
extern compass::Cal g_cal;
extern SemaphoreHandle_t g_stateMutex;
extern const char* g_hwName;
extern volatile uint8_t g_calCmd;       // display task -> loop()
extern volatile bool g_wifiOn;          // access point + web server running
extern volatile bool g_otaActive;       // firmware upload in progress
extern volatile uint32_t g_otaBytes;

#define LOG(level, ...)                                  \
  do {                                                   \
    if (g_settings.debug >= (level)) Serial.printf(__VA_ARGS__); \
  } while (0)

void appInit();
bool stateSnapshot(compass::State& out, uint32_t timeoutMs);  // false on lock timeout
void statePublish(const compass::State& s);
