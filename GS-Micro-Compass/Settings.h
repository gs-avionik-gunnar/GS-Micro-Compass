#pragma once
#include <stddef.h>
#include <stdint.h>

#include "Compass.h"

struct Settings {
  // WiFi (access point, only while switched on)
  char ssid[33];
  char wpakey[64];
  bool wifiBoot;         // switch WiFi on at power-up (off = quietest magnetic readings)
  // Display
  uint8_t dialBright;    // display backlight, 10..100 %
  uint8_t style;         // 0 instrument (with heading bug), 1 whiskey compass, 2 vertical card
  uint8_t startPage;     // page shown after boot: 0 compass, 1 WiFi, 2 info
  uint16_t bug;          // heading bug, 0..359 degrees (instrument style)
  // Compass
  int16_t offsetDeg;     // added to the heading, -180..180 degrees (mounting correction)
  int16_t declTenths;    // magnetic declination in 0.1 degree, -300..300 (east positive)
  uint8_t smooth;        // smoothing: 5..100 %, 100 = off
  bool swapXY, invX, invY;  // sensor mounting
  // Debug
  uint8_t debug;         // 0 off, 1 heading on serial, 2 raw values for the serial plotter
};

void settingsDefaults(Settings& s);
void settingsSanitize(Settings& s);  // single source of truth for all limits
void settingsLoad(Settings& s);      // loads from NVS, migrates the 1.x settings on first boot
bool settingsSave(const Settings& s);
void settingsFactoryReset();         // clears only the settings namespace (the calibration stays)
// Default SSID "GS-MC-XXXX" (last 4 hex digits of the MAC) and key = MAC (12 hex digits).
void settingsDefaultCredentials(char* ssid, size_t ssidCap, char* key, size_t keyCap);

// Calibration lives in its own namespace: a factory reset does not delete it.
bool calLoad(compass::Cal& c);
bool calSave(const compass::Cal& c);
