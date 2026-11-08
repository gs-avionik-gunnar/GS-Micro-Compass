// Defaults and limits of all settings. Pure C++ (testable on a PC); persistence is in Settings.cpp.
#include <stdio.h>
#include <string.h>

#include "Settings.h"

void settingsDefaults(Settings& s) {
  memset(&s, 0, sizeof s);
  settingsDefaultCredentials(s.ssid, sizeof s.ssid, s.wpakey, sizeof s.wpakey);
  s.wifiBoot = false;
  s.dialBright = 80;
  s.style = 0;
  s.startPage = 0;
  s.bug = 0;
  // defaults = the mounting used so far (sensor Y towards the back, X along the display)
  s.offsetDeg = 180;
  s.declTenths = 40;  // about +4.0 degrees in northern Germany
  s.smooth = 15;
  s.swapXY = true;
  s.invX = true;
  s.invY = false;
  s.debug = 0;
}

static int clampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void settingsSanitize(Settings& s) {
  s.ssid[sizeof s.ssid - 1] = '\0';
  s.wpakey[sizeof s.wpakey - 1] = '\0';
  if (strlen(s.ssid) < 1 || strlen(s.wpakey) < 8) {
    char d1[33], d2[64];
    settingsDefaultCredentials(d1, sizeof d1, d2, sizeof d2);
    if (strlen(s.ssid) < 1) strcpy(s.ssid, d1);
    if (strlen(s.wpakey) < 8) strcpy(s.wpakey, d2);
  }
  s.dialBright = (uint8_t)clampI(s.dialBright, 10, 100);
  s.style = (uint8_t)clampI(s.style, 0, 2);
  s.startPage = (uint8_t)clampI(s.startPage, 0, 2);
  s.bug = (uint16_t)(s.bug % 360);
  int off = s.offsetDeg % 360;  // 0..359 from the old firmware becomes -180..180
  if (off > 180) off -= 360;
  if (off < -180) off += 360;
  s.offsetDeg = (int16_t)off;
  s.declTenths = (int16_t)clampI(s.declTenths, -300, 300);
  s.smooth = (uint8_t)clampI(s.smooth, 5, 100);
  s.debug = (uint8_t)clampI(s.debug, 0, 2);
}
