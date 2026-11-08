#include "Settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#if __has_include("esp_mac.h")
#include "esp_mac.h"  // Arduino-ESP32 3.x: esp_efuse_mac_get_default() lives here
#endif
#include <stdio.h>
#include <string.h>

// NVS keys are limited to 15 characters.
static const char* kNs = "gsmc2";        // settings
static const char* kNsCal = "gsmcal";    // calibration (survives a factory reset)
static const char* kNsOld = "gsmc";      // firmware 1.x (read once for the migration)
static const uint32_t kCfgVersion = 1;

void settingsDefaultCredentials(char* ssid, size_t ssidCap, char* key, size_t keyCap) {
  uint8_t mac[6] = {0};
  esp_efuse_mac_get_default(mac);  // readable before the radio is started
  snprintf(ssid, ssidCap, "GS-MC-%02X%02X", mac[4], mac[5]);
  snprintf(key, keyCap, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void readAll(Preferences& p, Settings& s) {
  String v;
  v = p.getString("ssid", s.ssid);
  strncpy(s.ssid, v.c_str(), sizeof s.ssid - 1);
  v = p.getString("wpakey", s.wpakey);
  strncpy(s.wpakey, v.c_str(), sizeof s.wpakey - 1);
  s.wifiBoot = p.getBool("wifiboot", s.wifiBoot);
  s.dialBright = p.getUChar("dlbr", s.dialBright);
  s.style = p.getUChar("style", s.style);
  s.startPage = p.getUChar("dlstart", s.startPage);
  s.bug = p.getUShort("bug", s.bug);
  s.offsetDeg = p.getShort("hoff", s.offsetDeg);
  s.declTenths = p.getShort("decl", s.declTenths);
  s.smooth = p.getUChar("smooth", s.smooth);
  s.swapXY = p.getBool("swapxy", s.swapXY);
  s.invX = p.getBool("invx", s.invX);
  s.invY = p.getBool("invy", s.invY);
  s.debug = p.getUChar("debug", s.debug);
}

static void writeAll(Preferences& p, const Settings& s) {
  p.putString("ssid", s.ssid);
  p.putString("wpakey", s.wpakey);
  p.putBool("wifiboot", s.wifiBoot);
  p.putUChar("dlbr", s.dialBright);
  p.putUChar("style", s.style);
  p.putUChar("dlstart", s.startPage);
  p.putUShort("bug", s.bug);
  p.putShort("hoff", s.offsetDeg);
  p.putShort("decl", s.declTenths);
  p.putUChar("smooth", s.smooth);
  p.putBool("swapxy", s.swapXY);
  p.putBool("invx", s.invX);
  p.putBool("invy", s.invY);
  p.putUChar("debug", s.debug);
  p.putUInt("cfgver", kCfgVersion);
}

// Firmware 1.x stored offset, declination, style and bug as ints in namespace "gsmc".
static void migrateLegacy(Settings& s) {
  Preferences old;
  if (!old.begin(kNsOld, true)) return;
  s.bug = (uint16_t)old.getInt("bug", s.bug);
  s.offsetDeg = (int16_t)old.getInt("hoff", s.offsetDeg);
  s.declTenths = (int16_t)old.getInt("decl", s.declTenths);
  s.style = (uint8_t)old.getInt("style", s.style);
  old.end();
}

void settingsLoad(Settings& s) {
  settingsDefaults(s);
  Preferences p;
  if (!p.begin(kNs, false)) {
    settingsSanitize(s);
    return;
  }
  if (p.getUInt("cfgver", 0) == 0) {
    migrateLegacy(s);
    settingsSanitize(s);
    writeAll(p, s);
  } else {
    readAll(p, s);
    settingsSanitize(s);
  }
  p.end();
}

bool settingsSave(const Settings& in) {
  Settings s = in;
  settingsSanitize(s);
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  writeAll(p, s);  // no clear(): a power loss mid-save can no longer wipe every setting
  p.end();
  return true;
}

void settingsFactoryReset() {
  Preferences p;
  if (p.begin(kNs, false)) {
    p.clear();  // only the settings namespace; the calibration is device data and stays
    p.end();
  }
}

// ---- calibration -------------------------------------------------------------------------------------------------
bool calSave(const compass::Cal& c) {
  Preferences p;
  if (!p.begin(kNsCal, false)) return false;
  p.putFloat("offx", c.offX);
  p.putFloat("offy", c.offY);
  p.putFloat("scx", c.scX);
  p.putFloat("scy", c.scY);
  p.putUChar("valid", 1);
  p.end();
  return true;
}

bool calLoad(compass::Cal& c) {
  c = compass::defaultCal();
  Preferences p;
  if (p.begin(kNsCal, false)) {
    if (p.getUChar("valid", 0) == 1) {
      c.offX = p.getFloat("offx", 0.0f);
      c.offY = p.getFloat("offy", 0.0f);
      c.scX = p.getFloat("scx", 1.0f);
      c.scY = p.getFloat("scy", 1.0f);
      c.valid = c.scX > 0.0f && c.scY > 0.0f;
      p.end();
      return c.valid;
    }
    p.end();
  }
  // first start of 2.x: take over the calibration of firmware 1.x (keys offX, offY, scX, scY, floats)
  Preferences old;
  if (old.begin(kNsOld, true)) {
    float sx = old.getFloat("scX", 0.0f), sy = old.getFloat("scY", 0.0f);
    if (sx > 0.0f && sy > 0.0f) {
      c.offX = old.getFloat("offX", 0.0f);
      c.offY = old.getFloat("offY", 0.0f);
      c.scX = sx;
      c.scY = sy;
      c.valid = true;
    }
    old.end();
    if (c.valid) calSave(c);
  }
  return c.valid;
}
