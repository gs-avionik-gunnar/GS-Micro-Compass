#include "WebUi.h"

#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "App.h"
#include "Compass.h"
#include "Config.h"
#include "WebPage.h"

static WebServer server(80);
static char g_json[1024];
static SemaphoreHandle_t g_webMutex = nullptr;  // protects server/WiFi against start/stop from the display task
static bool g_otaOk = false;
static bool g_routes = false;

static void jf(char* b, size_t cap, size_t& len, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
static void jf(char* b, size_t cap, size_t& len, const char* fmt, ...) {
  if (len >= cap) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(b + len, cap - len, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  len += (size_t)n;
  if (len >= cap) len = cap - 1;
}

static void restartSoon() {
  delay(600);  // let the HTTP response leave the socket
  ESP.restart();
}

// Declination in tenths -> "-1.5"
static void fmtTenths(char* b, size_t n, int t) {
  int a = t < 0 ? -t : t;
  snprintf(b, n, "%s%d.%d", t < 0 ? "-" : "", a / 10, a % 10);
}

// ------------------------------------------------------------------ status
static void handleStatus() {
  compass::State st;
  if (!stateSnapshot(st, 100)) {
    server.send(503, "text/plain", "busy");
    return;
  }
  const uint32_t now = millis();
  const bool fresh = st.sensor != compass::kSensorNone && st.fresh(now);
  const float hdg = compass::applyOffsets(st.rawDeg, g_settings.declTenths, g_settings.offsetDeg);
  // status LED dot in the header: green = live heading, red = no sensor, orange = calibrating / no data
  const int led = st.sensor == compass::kSensorNone ? 2 : (st.calActive || !fresh ? 3 : 1);
  char decl[12];
  fmtTenths(decl, sizeof decl, g_settings.declTenths);
  size_t len = 0;
  jf(g_json, sizeof g_json, len,
     "{\"fw\":\"%s\",\"hw\":\"%s\",\"up\":%lu,\"heap\":%u,\"minHeap\":%u,\"led\":%d,"
     "\"heading\":%.1f,\"valid\":%s,\"bug\":%u,\"style\":%u,\"sensor\":\"%s\",\"calibrated\":%s,\"calActive\":%s,"
     "\"spanX\":%.0f,\"spanY\":%.0f,\"rawX\":%d,\"rawY\":%d,\"rawZ\":%d,\"offset\":%d,\"decl\":%s,\"stations\":%d}",
     GS_FW_VERSION, g_hwName, (unsigned long)(now / 1000), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), led,
     (double)hdg, fresh ? "true" : "false", (unsigned)g_settings.bug, (unsigned)g_settings.style, compass::sensorName(st.sensor),
     st.calibrated ? "true" : "false", st.calActive ? "true" : "false", (double)st.spanX, (double)st.spanY, st.rawX, st.rawY, st.rawZ,
     (int)g_settings.offsetDeg, decl, (int)WiFi.softAPgetStationNum());
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", g_json);
}

// ------------------------------------------------------------------ config
// The WPA key is never returned. Field names are the keys of this object.
static void handleGetConfig() {
  const Settings& s = g_settings;
  char ssid[70];
  size_t j = 0;
  for (const char* p = s.ssid; *p && j < sizeof ssid - 3; ++p) {
    if (*p == '"' || *p == '\\') ssid[j++] = '\\';
    ssid[j++] = *p;
  }
  ssid[j] = 0;
  char decl[12];
  fmtTenths(decl, sizeof decl, s.declTenths);
  size_t len = 0;
  jf(g_json, sizeof g_json, len,
     "{\"ssid\":\"%s\",\"wifiBoot\":%d,\"dialBright\":%u,\"style\":%u,\"startPage\":%u,\"bug\":%u,"
     "\"offsetDeg\":%d,\"decl\":%s,\"smooth\":%u,\"swapXY\":%d,\"invX\":%d,\"invY\":%d,\"debug\":%u}",
     ssid, s.wifiBoot, (unsigned)s.dialBright, (unsigned)s.style, (unsigned)s.startPage, (unsigned)s.bug, (int)s.offsetDeg, decl,
     (unsigned)s.smooth, s.swapXY, s.invX, s.invY, (unsigned)s.debug);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", g_json);
}

static bool argBool(const char* name) { return server.arg(name) == "on"; }

// Strict integer field. Absent field: keeps `val`. Present but empty, non-numeric or out of range:
// returns false and fills `err`.
static bool argInt(const char* name, const char* label, long lo, long hi, long& val, String& err) {
  if (!server.hasArg(name)) return true;
  String v = server.arg(name);
  v.trim();
  bool ok = v.length() > 0 && v.length() <= 10;
  for (unsigned i = 0; ok && i < v.length(); ++i)
    if (!isdigit((unsigned char)v[i]) && !(i == 0 && v[i] == '-' && v.length() > 1)) ok = false;
  long r = 0;
  if (ok) {
    r = strtol(v.c_str(), nullptr, 10);
    ok = (r >= lo && r <= hi);
  }
  if (!ok) {
    char buf[96];
    snprintf(buf, sizeof buf, "%s must be a whole number from %ld to %ld", label, lo, hi);
    err = buf;
    return false;
  }
  val = r;
  return true;
}

// Declination in degrees with at most one decimal (also accepts a comma), result in tenths.
static bool argTenths(const char* name, const char* label, int lo, int hi, int& val, String& err) {
  if (!server.hasArg(name)) return true;
  String v = server.arg(name);
  v.trim();
  v.replace(",", ".");
  bool ok = v.length() > 0 && v.length() <= 8;
  int dots = 0;
  for (unsigned i = 0; ok && i < v.length(); ++i) {
    if (v[i] == '.') ok = ++dots == 1;
    else if (!isdigit((unsigned char)v[i]) && !(i == 0 && v[i] == '-' && v.length() > 1)) ok = false;
  }
  long t = 0;
  if (ok) {
    double d = atof(v.c_str());
    t = (long)(d * 10.0 + (d < 0 ? -0.5 : 0.5));
    ok = t >= lo && t <= hi;
  }
  if (!ok) {
    char buf[96];
    snprintf(buf, sizeof buf, "%s must be a number from %.1f to %.1f", label, lo / 10.0, hi / 10.0);
    err = buf;
    return false;
  }
  val = (int)t;
  return true;
}

#define FAIL(msg) return server.send(400, "text/plain", (msg))
#define NUM(var, name, label, lo, hi)                             \
  do {                                                            \
    long _v = (long)(var);                                        \
    if (!argInt(name, label, lo, hi, _v, err)) FAIL(err.c_str()); \
    (var) = (decltype(var))_v;                                    \
  } while (0)

static void handlePostConfig() {
  Settings s = g_settings;  // start from the running config so missing fields keep their value
  String err;

  String ssid = server.arg("ssid");
  if (ssid.length() < 1 || ssid.length() > 32) FAIL("SSID must be 1-32 characters");
  strncpy(s.ssid, ssid.c_str(), sizeof s.ssid - 1);
  s.ssid[sizeof s.ssid - 1] = 0;

  String key = server.arg("wpakey");  // empty = keep the current key
  if (key.length() > 0) {
    if (key.length() < 8 || key.length() > 63) FAIL("WPA key must be 8-63 characters");
    strncpy(s.wpakey, key.c_str(), sizeof s.wpakey - 1);
    s.wpakey[sizeof s.wpakey - 1] = 0;
  }
  s.wifiBoot = argBool("wifiBoot");

  NUM(s.dialBright, "dialBright", "Brightness", 10, 100);
  NUM(s.style, "style", "Style", 0, 2);
  NUM(s.startPage, "startPage", "Start page", 0, 2);
  NUM(s.bug, "bug", "Heading bug", 0, 359);
  NUM(s.offsetDeg, "offsetDeg", "Offset", -180, 180);
  {
    int t = s.declTenths;
    if (!argTenths("decl", "Declination", -300, 300, t, err)) FAIL(err.c_str());
    s.declTenths = (int16_t)t;
  }
  NUM(s.smooth, "smooth", "Smoothing", 5, 100);
  s.swapXY = argBool("swapXY");
  s.invX = argBool("invX");
  s.invY = argBool("invY");
  NUM(s.debug, "debug", "Serial output", 0, 2);

  settingsSanitize(s);
  if (!settingsSave(s)) {
    server.send(500, "text/plain", "Could not save the settings");
    return;
  }
  server.send(200, "text/plain", "OK");
  restartSoon();
}

static void handleReset() {
  settingsFactoryReset();
  server.send(200, "text/plain", "OK");
  restartSoon();
}

// --------------------------------------------------------------------- OTA
static void handleUpdateDone() {
  server.sendHeader("Connection", "close");
  if (g_otaOk) {
    server.send(200, "text/plain", "OK");
    restartSoon();
  } else {
    server.send(500, "text/plain", Update.hasError() ? Update.errorString() : "no file received");
  }
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_otaOk = false;
    g_otaBytes = 0;
    g_otaActive = true;
    LOG(1, "OTA: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    g_otaBytes += (uint32_t)up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      g_otaOk = true;
      LOG(1, "OTA ok: %u bytes\n", (unsigned)up.totalSize);
    } else {
      Update.printError(Serial);
    }
    g_otaActive = false;
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    g_otaActive = false;
  }
}

// -------------------------------------------------------------------- task
static void webTask(void*) {
  for (;;) {
    if (g_wifiOn && xSemaphoreTake(g_webMutex, 0) == pdTRUE) {
      server.handleClient();
      xSemaphoreGive(g_webMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void webBegin() {
  g_webMutex = xSemaphoreCreateMutex();
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", kIndexHtml); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/reset", HTTP_POST, handleReset);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  g_routes = true;
  xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0);
}

void webStart() {
  if (!g_routes || g_wifiOn) return;
  xSemaphoreTake(g_webMutex, portMAX_DELAY);
  IPAddress ip(AP_IP_A, AP_IP_B, AP_IP_C, AP_IP_D);
  IPAddress mask(255, 255, 255, 0);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAPConfig(ip, ip, mask)) Serial.println("softAPConfig failed");
  if (!WiFi.softAP(g_settings.ssid, g_settings.wpakey, AP_CHANNEL, 0, AP_MAX_STATIONS)) Serial.println("softAP failed");
  server.begin();
  g_wifiOn = true;
  xSemaphoreGive(g_webMutex);
  Serial.printf("WiFi AP '%s' started, IP %s\n", g_settings.ssid, ip.toString().c_str());
}

void webStop() {
  if (!g_routes || !g_wifiOn || g_otaActive) return;
  xSemaphoreTake(g_webMutex, portMAX_DELAY);
  g_wifiOn = false;
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  xSemaphoreGive(g_webMutex);
  Serial.println("WiFi off");
}
