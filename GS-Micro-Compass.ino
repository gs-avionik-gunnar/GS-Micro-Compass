/*
  GS-Micro-Compass
  ----------------
  Vertikal-Kompass (drehende Rose, feste Kurslinie oben) im Stil eines
  Flugzeug-Instruments mit Heading-Bug.

  Hardware : M5Stack M5 Dial (ESP32-S3, 240x240 rund, Drehgeber + Taste)
             GY-271 Magnetometer (QMC5883L oder HMC5883L, wird automatisch erkannt)
             an Port A (SDA = G13, SCL = G15)

  Bedienung:
    - Drehrad          : Heading-Bug setzen (1 Grad pro Rastung)
    - Taste kurz       : Bug auf aktuellen Kurs setzen
    - Taste >1 s halten: Menü
        a) Kalibrieren   (Sensor 20 s in allen Richtungen drehen,
                          Taste kurz = vorzeitig beenden)
        b) Offset        (Drehrad ändert den Offset, Taste = speichern)
        c) Info          (Version, Sensor, Kalibrier- und Offsetwerte)
        d) Deklination   (Drehrad ändert in 0,1-Grad-Schritten, Taste = speichern)
        e) Stil          (1 Instrument mit Heading-Bug, 2 Schnapskompass, 3 Vertikalkartenkompass;
                          2 und 3 ohne Bug)
        f) Update        (WLAN-Access-Point "GS-MC-XXXX", Passwort = MAC; Firmware-Upload
                          per Browser unter http://192.168.4.1)

  Libraries : nur "M5Dial" (bringt M5Unified + M5GFX mit), sonst Arduino-Core
              (Wire, Preferences).
  Board     : "M5Dial" bzw. M5Stack-Boardpaket -> M5Dial
*/

#include <M5Dial.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>        // Teil des ESP32-Cores
#include <WebServer.h>   // Teil des ESP32-Cores
#include <Update.h>      // Teil des ESP32-Cores

// ======================= Konfiguration =======================
#define PROJECT_NAME        "GS-Micro-Compass"
#define BOOT_LINE1          "GS-Avionik"
#define BOOT_LINE2          "Micro-Compass"
#define PROJECT_VERSION     "1.5.0"

#define PIN_SDA             13      // Port A am M5 Dial
#define PIN_SCL             15

// Magnetische Missweisung (Deklination) in Grad, Ost = positiv.
// Schleswig-Holstein ca. +4.0 Grad. 0.0 = magnetischer Kurs.
#define MAG_DECLINATION_DEG 4.0f   // Standardwert; im Menü änderbar und dann gespeichert

// Einbaulage anpassen, falls der Kurs falsch herum läuft / verdreht ist:
#define MAG_SWAP_XY         true    // X und Y vertauschen
#define MAG_INVERT_X        true 
#define MAG_INVERT_Y        false
#define HEADING_OFFSET_DEG  180.0f  // Standardwert; im Menü änderbar und dann gespeichert

#define BUG_STEP_DEG        1       // Grad pro Encoder-Schritt
#define ENCODER_DIV         1       // ggf. 2 oder 4, falls Rad pro Rastung mehrere Schritte liefert
#define FILTER_ALPHA        0.15f   // Glättung (0..1, kleiner = ruhiger)
// Vertikalkarten-Stil: true = höhere Werte links von der Kurslinie (wie im Vorbild-Foto),
// false = höhere Werte rechts (wie bei einer Rose von oben gesehen).
#define VCARD_HIGHER_LEFT   true
#define VCARD_PX_PER_DEG    2.0f    // Maßstab der Kartenanzeige
#define CAL_DURATION_MS     20000UL
#define MENU_HOLD_MS        1000UL  // Haltezeit der Taste für das Menü

// ======================= Farben =======================
#define COL_BG      TFT_BLACK
#define COL_CARD    TFT_WHITE
#define COL_N       TFT_ORANGE
#define COL_BUG     TFT_CYAN
#define COL_PLANE   TFT_YELLOW

// ======================= Globals =======================
static const int CX = 120, CY = 120;
static const uint8_t ADDR_QMC = 0x0D;
static const uint8_t ADDR_HMC = 0x1E;

enum SensorType { SENSOR_NONE, SENSOR_QMC, SENSOR_HMC };
static SensorType sensorType = SENSOR_NONE;

M5Canvas canvas(&M5Dial.Display);
Preferences prefs;

// Kalibrierwerte (Hard-/Soft-Iron, nur X/Y da Kompass waagerecht liegt)
static float offX = 0, offY = 0, scaleX = 1, scaleY = 1;

static float filtX = 0, filtY = 0;
static bool  filtInit = false;
static float heading = 0;        // 0..360
static int   bugHdg  = 0;        // 0..359
static int   headingOffset = (int)HEADING_OFFSET_DEG;  // 0..359, im Menü einstellbar
enum Style { STYLE_INSTRUMENT = 0, STYLE_WHISKEY = 1, STYLE_VCARD = 2, STYLE_COUNT = 3 };
static int   dispStyle = STYLE_INSTRUMENT;   // Darstellungsstil, im Menü einstellbar
static int   declTenths = (int)lroundf(MAG_DECLINATION_DEG * 10.0f);  // Deklination in 0,1 Grad, -300..300
static bool  bugDirty = false;
static uint32_t bugChangedAt = 0;

// ======================= I2C Helfer =======================
static bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool i2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// ======================= Sensor =======================
static bool sensorBegin() {
  if (i2cProbe(ADDR_QMC)) {
    sensorType = SENSOR_QMC;
    i2cWrite(ADDR_QMC, 0x0B, 0x01);          // SET/RESET period
    // OSR 512, Range 8G, ODR 200Hz, Continuous
    return i2cWrite(ADDR_QMC, 0x09, 0x1D);
  }
  if (i2cProbe(ADDR_HMC)) {
    sensorType = SENSOR_HMC;
    i2cWrite(ADDR_HMC, 0x00, 0x78);          // 8x Mittelung, 75 Hz
    i2cWrite(ADDR_HMC, 0x01, 0x20);          // Gain +-1.3 Ga
    return i2cWrite(ADDR_HMC, 0x02, 0x00);   // Continuous
  }
  sensorType = SENSOR_NONE;
  return false;
}

static bool sensorRead(int16_t &x, int16_t &y) {
  uint8_t b[6];
  if (sensorType == SENSOR_QMC) {
    Wire.beginTransmission(ADDR_QMC);
    Wire.write(0x06);                         // Status
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)ADDR_QMC, 1) != 1) return false;
    if (!(Wire.read() & 0x01)) return false;  // DRDY

    Wire.beginTransmission(ADDR_QMC);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)ADDR_QMC, 6) != 6) return false;
    for (int i = 0; i < 6; i++) b[i] = Wire.read();
    x = (int16_t)(b[1] << 8 | b[0]);
    y = (int16_t)(b[3] << 8 | b[2]);
    return true;
  }
  if (sensorType == SENSOR_HMC) {
    Wire.beginTransmission(ADDR_HMC);
    Wire.write(0x03);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)ADDR_HMC, 6) != 6) return false;
    for (int i = 0; i < 6; i++) b[i] = Wire.read();
    x = (int16_t)(b[0] << 8 | b[1]);          // Reihenfolge X, Z, Y
    y = (int16_t)(b[4] << 8 | b[5]);
    if (x == -4096 || y == -4096) return false;
    return true;
  }
  return false;
}

// ======================= Kalibrierung =======================
static void loadCalibration() {
  prefs.begin("gsmc", true);
  offX   = prefs.getFloat("offX", 0);
  offY   = prefs.getFloat("offY", 0);
  scaleX = prefs.getFloat("scX", 1);
  scaleY = prefs.getFloat("scY", 1);
  bugHdg = prefs.getInt("bug", 0);
  headingOffset = prefs.getInt("hoff", (int)HEADING_OFFSET_DEG);
  declTenths = prefs.getInt("decl", declTenths);
  dispStyle = prefs.getInt("style", STYLE_INSTRUMENT);
  if (dispStyle < 0 || dispStyle >= STYLE_COUNT) dispStyle = STYLE_INSTRUMENT;
  prefs.end();
}

static void saveCalibration() {
  prefs.begin("gsmc", false);
  prefs.putFloat("offX", offX);
  prefs.putFloat("offY", offY);
  prefs.putFloat("scX", scaleX);
  prefs.putFloat("scY", scaleY);
  prefs.end();
}

static void saveOffset() {
  prefs.begin("gsmc", false);
  prefs.putInt("hoff", headingOffset);
  prefs.end();
}

static void saveDeclination() {
  prefs.begin("gsmc", false);
  prefs.putInt("decl", declTenths);
  prefs.end();
}

static void saveStyle() {
  prefs.begin("gsmc", false);
  prefs.putInt("style", dispStyle);
  prefs.end();
}

static void saveBug() {
  prefs.begin("gsmc", false);
  prefs.putInt("bug", bugHdg);
  prefs.end();
}

static void runCalibration() {
  int16_t x, y;
  float minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
  uint32_t t0 = millis();

  while (millis() - t0 < CAL_DURATION_MS) {
    M5Dial.update();
    if (M5Dial.BtnA.wasClicked()) break;

    if (sensorRead(x, y)) {
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
    }

    // Anzeige
    canvas.fillSprite(COL_BG);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_PLANE);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.drawString("KALIBRIERUNG", CX, 50);
    canvas.setTextColor(TFT_WHITE);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.drawString("Sensor in allen", CX, 90);
    canvas.drawString("Richtungen drehen", CX, 112);

    int remain = (CAL_DURATION_MS - (millis() - t0)) / 1000 + 1;
    char buf[24];
    snprintf(buf, sizeof(buf), "%d s", remain);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextColor(COL_BUG);
    canvas.drawString(buf, CX, 155);

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(TFT_DARKGREY);
    canvas.drawString("Taste = fertig", CX, 200);
    canvas.pushSprite(0, 0);
    delay(20);
  }

  float rx = maxX - minX, ry = maxY - minY;
  bool ok = (rx > 50 && ry > 50);     // plausibler Messbereich?
  if (ok) {
    offX = (maxX + minX) / 2.0f;
    offY = (maxY + minY) / 2.0f;
    float avg = (rx + ry) / 2.0f;
    scaleX = avg / rx;
    scaleY = avg / ry;
    saveCalibration();
  }

  canvas.fillSprite(COL_BG);
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(ok ? TFT_GREEN : TFT_RED);
  canvas.drawString(ok ? "Gespeichert" : "Fehlgeschlagen", CX, CY);
  canvas.pushSprite(0, 0);
  delay(1200);
  filtInit = false;
}

// ======================= Kurs berechnen =======================
static void updateHeading() {
  int16_t rx, ry;
  if (!sensorRead(rx, ry)) return;

  float x = ((float)rx - offX) * scaleX;
  float y = ((float)ry - offY) * scaleY;
#if MAG_SWAP_XY
  { float t = x; x = y; y = t; }
#endif
#if MAG_INVERT_X
  x = -x;
#endif
#if MAG_INVERT_Y
  y = -y;
#endif

  // Vektor filtern (kein Problem mit 359->0 Sprung)
  if (!filtInit) { filtX = x; filtY = y; filtInit = true; }
  else {
    filtX += FILTER_ALPHA * (x - filtX);
    filtY += FILTER_ALPHA * (y - filtY);
  }

  float h = atan2f(filtY, filtX) * RAD_TO_DEG;
  h += declTenths / 10.0f + (float)headingOffset;
  while (h < 0)    h += 360.0f;
  while (h >= 360) h -= 360.0f;
  heading = h;
}

// ======================= Zeichnen =======================
static inline void polar(float r, float angDeg, int &x, int &y) {
  float a = angDeg * DEG_TO_RAD;       // 0 = oben, im Uhrzeigersinn
  x = lroundf(CX + r * sinf(a));
  y = lroundf(CY - r * cosf(a));
}

static void drawCard() {
  int x0, y0, x1, y1;
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(middle_center);

  for (int d = 0; d < 360; d += 5) {
    float ang = d - heading;
    bool major = (d % 10 == 0);
    bool label = (d % 30 == 0);

    polar(118, ang, x0, y0);
    polar(label ? 100 : (major ? 106 : 112), ang, x1, y1);
    if (label) canvas.drawWideLine(x0, y0, x1, y1, 3, COL_CARD);
    else       canvas.drawLine(x0, y0, x1, y1, COL_CARD);

    if (label) {
      char t[4];
      switch (d) {
        case 0:   strcpy(t, "N"); break;
        case 90:  strcpy(t, "E"); break;
        case 180: strcpy(t, "S"); break;
        case 270: strcpy(t, "W"); break;
        default:  snprintf(t, sizeof(t), "%d", d / 10); break;
      }
      polar(86, ang, x0, y0);
      canvas.setTextColor(d == 0 ? COL_N : COL_CARD);
      canvas.drawString(t, x0, y0);
    }
  }
}

static void drawBug() {
  float a = bugHdg - heading;
  int ax, ay, bx, by, cx, cy, dx, dy;
  polar(119, a - 5, ax, ay);
  polar(119, a + 5, bx, by);
  polar(102, a + 5, cx, cy);
  polar(102, a - 5, dx, dy);
  canvas.fillTriangle(ax, ay, bx, by, cx, cy, COL_BUG);
  canvas.fillTriangle(ax, ay, cx, cy, dx, dy, COL_BUG);
  // Aussparung, damit es wie ein Bug-Bügel aussieht
  int ex, ey, fx, fy, gx, gy;
  polar(114, a - 2, ex, ey);
  polar(114, a + 2, fx, fy);
  polar(104, a, gx, gy);
  canvas.fillTriangle(ex, ey, fx, fy, gx, gy, COL_BG);
}

static void drawPlaneAndReadouts() {
  // Feste Kurslinie (Lubber Line) oben
  canvas.fillTriangle(CX - 8, 0, CX + 8, 0, CX, 16, COL_PLANE);

  // Flugzeugsymbol in der Mitte
  canvas.drawWideLine(CX, CY - 22, CX, CY + 24, 4, COL_PLANE);       // Rumpf
  canvas.drawWideLine(CX - 30, CY + 2, CX + 30, CY + 2, 4, COL_PLANE); // Tragflaechen
  canvas.drawWideLine(CX - 11, CY + 22, CX + 11, CY + 22, 3, COL_PLANE); // Leitwerk

  // Digitale Kursanzeige oben
  char buf[16];
  int h = (int)lroundf(heading) % 360;
  snprintf(buf, sizeof(buf), "%03d", h);
  canvas.fillRoundRect(CX - 30, 46, 60, 28, 4, COL_BG);
  canvas.drawRoundRect(CX - 30, 46, 60, 28, 4, COL_CARD);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(COL_CARD);
  canvas.drawString(buf, CX, 61);

  // Bug-Anzeige unten
  snprintf(buf, sizeof(buf), "BUG %03d", bugHdg);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(COL_BUG);
  canvas.drawString(buf, CX, 182);
}

// ======================= Firmware-Update per WLAN =======================
static const char PAGE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GS-Micro-Compass</title>
<style>
body{font-family:sans-serif;background:#111;color:#eee;text-align:center;margin:0;padding:24px}
h1{color:#3c3;margin:4px}h2{color:#3c3;margin:4px 0 16px}
button,input{font-size:1em;margin:6px}
button{padding:10px 20px;background:#3c3;border:0;border-radius:6px;color:#000}
progress{width:90%;height:20px}
</style></head><body>
<h1>GS-Avionik</h1><h2>Micro-Compass</h2>
<p>Aktuelle Version: %VER%</p>
<p>Firmware-Datei (.bin) auswählen:</p>
<input type="file" id="f" accept=".bin"><br>
<button id="b" onclick="up()">Update starten</button><br>
<progress id="p" value="0" max="100" hidden></progress>
<p id="m"></p>
<script>
function m(t){document.getElementById('m').textContent=t}
function up(){
  var f=document.getElementById('f').files[0];
  if(!f){m('Bitte zuerst eine .bin-Datei auswählen');return}
  var p=document.getElementById('p'),b=document.getElementById('b');
  var d=new FormData();d.append('firmware',f,f.name);
  var x=new XMLHttpRequest();
  x.upload.onprogress=function(e){if(e.lengthComputable){p.hidden=false;p.value=e.loaded/e.total*100}};
  x.onload=function(){b.disabled=false;m(x.responseText=='OK'?'Update erfolgreich - der Compass startet neu':'Update fehlgeschlagen: '+x.responseText)};
  x.onerror=function(){b.disabled=false;m('Verbindung unterbrochen')};
  b.disabled=true;m('Übertragung läuft - bitte nicht trennen');
  x.open('POST','/update');x.send(d);
}
</script></body></html>)HTML";

static WebServer server(80);
static String   apSsid, apPass;
static bool     otaRoutesSet = false;
static bool     otaActive    = false;    // Upload läuft
static bool     otaFinished  = false;    // Update erfolgreich geschrieben
static bool     otaFailed    = false;
static uint32_t otaBytes     = 0;
static uint32_t otaRestartAt = 0;
static char     otaError[40] = "";

static void handleRoot() {
  String page(PAGE_HTML);
  page.replace("%VER%", PROJECT_VERSION);
  server.send(200, "text/html", page);
}

static void handleUpdateDone() {
  bool ok = otaFinished && !otaFailed;
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : (otaError[0] ? otaError : "Fehler"));
  if (ok) otaRestartAt = millis() + 1500;
}

static void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaActive = true; otaFinished = false; otaFailed = false;
    otaBytes = 0; otaError[0] = 0;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      otaFailed = true;
      strncpy(otaError, Update.errorString(), sizeof(otaError) - 1);
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!otaFailed) {
      if (Update.write(up.buf, up.currentSize) != up.currentSize) {
        otaFailed = true;
        strncpy(otaError, Update.errorString(), sizeof(otaError) - 1);
      } else {
        otaBytes += up.currentSize;
      }
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!otaFailed) {
      if (Update.end(true)) {
        otaFinished = true;
      } else {
        otaFailed = true;
        strncpy(otaError, Update.errorString(), sizeof(otaError) - 1);
      }
    }
    otaActive = false;
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaFailed = true;
    otaActive = false;
    strncpy(otaError, "Abgebrochen", sizeof(otaError) - 1);
  }
}

static void startUpdateMode() {
  otaActive = false; otaFinished = false; otaFailed = false;
  otaBytes = 0; otaError[0] = 0;

  WiFi.mode(WIFI_AP);
  String mac = WiFi.macAddress();           // z.B. "A1:B2:C3:D4:E5:F6"
  mac.replace(":", "");
  mac.toUpperCase();
  apPass = mac;                             // 12 Zeichen, erfüllt WPA2 (min. 8)
  apSsid = "GS-MC-" + mac.substring(8);     // letzte 4 Stellen der MAC
  WiFi.softAP(apSsid.c_str(), apPass.c_str());

  if (!otaRoutesSet) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
    otaRoutesSet = true;
  }
  server.begin();
}

static void stopUpdateMode() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

static void drawUpdateScreen() {
  char buf[40];
  canvas.fillSprite(COL_BG);
  canvas.setTextDatum(middle_center);

  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(COL_PLANE);
  canvas.drawString("UPDATE", CX, 34);

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("WLAN", CX, 62);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(COL_BUG);
  canvas.drawString(apSsid.c_str(), CX, 83);

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("Passwort", CX, 111);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(COL_CARD);
  canvas.drawString(apPass.c_str(), CX, 132);

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(COL_CARD);
  canvas.drawString("http://192.168.4.1", CX, 162);

  if (otaFinished) {
    canvas.setTextColor(TFT_GREEN);
    canvas.drawString("Fertig - Neustart", CX, 189);
  } else if (otaFailed) {
    canvas.setTextColor(TFT_RED);
    snprintf(buf, sizeof(buf), "Fehler: %s", otaError);
    canvas.drawString(buf, CX, 189);
  } else if (otaActive) {
    canvas.setTextColor(COL_PLANE);
    snprintf(buf, sizeof(buf), "Empfange %u kB", (unsigned)(otaBytes / 1024));
    canvas.drawString(buf, CX, 189);
  } else {
    canvas.setTextColor(TFT_GREEN);
    snprintf(buf, sizeof(buf), "%d Client(s) verbunden", WiFi.softAPgetStationNum());
    canvas.drawString(buf, CX, 189);
  }

  if (!otaActive && !otaFinished) {
    canvas.setTextColor(TFT_DARKGREY);
    canvas.drawString("Taste = Abbrechen", CX, 212);
  }
  canvas.pushSprite(0, 0);
}

// ======================= Menü =======================
enum Mode { MODE_COMPASS, MODE_MENU, MODE_OFFSET, MODE_INFO, MODE_DECL, MODE_STYLE, MODE_UPDATE };
static Mode mode = MODE_COMPASS;
static int  menuSel = 0;

static const char* MENU_ITEMS[] = { "Kalibrieren", "Offset", "Info", "Deklination", "Stil", "Update", "Zurueck" };
static const int   MENU_COUNT   = 7;

// Encoder-Schritte seit dem letzten Aufruf (mit Teiler ENCODER_DIV)
static int encoderSteps() {
  static long acc = 0;
  acc += M5Dial.Encoder.readAndReset();
  long st = acc / ENCODER_DIV;
  acc -= st * ENCODER_DIV;
  return (int)st;
}

static void drawMenu() {
  canvas.fillSprite(COL_BG);
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(COL_PLANE);
  canvas.drawString("MENUE", CX, 30);

  canvas.setFont(&fonts::FreeSansBold9pt7b);
  for (int i = 0; i < MENU_COUNT; i++) {
    int y = 58 + i * 25;
    if (i == menuSel) {
      canvas.fillRoundRect(CX - 62, y - 11, 124, 22, 8, COL_BUG);
      canvas.setTextColor(COL_BG);
    } else {
      canvas.setTextColor(COL_CARD);
    }
    canvas.drawString(MENU_ITEMS[i], CX, y);
  }
  canvas.pushSprite(0, 0);
}

static void drawOffsetScreen() {
  char buf[24];
  canvas.fillSprite(COL_BG);
  canvas.setTextDatum(middle_center);

  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(COL_PLANE);
  canvas.drawString("OFFSET", CX, 38);

  // Live-Kurs mit aktuellem Offset
  snprintf(buf, sizeof(buf), "%03d", (int)lroundf(heading) % 360);
  canvas.setFont(&fonts::FreeSansBold24pt7b);
  canvas.setTextColor(COL_CARD);
  canvas.drawString(buf, CX, 95);

  snprintf(buf, sizeof(buf), "Offset %d", headingOffset);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(COL_BUG);
  canvas.drawString(buf, CX, 145);

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("Drehen = +/- 1", CX, 185);
  canvas.drawString("Taste = speichern", CX, 207);
  canvas.pushSprite(0, 0);
}

static void drawDeclScreen() {
  char buf[24];
  canvas.fillSprite(COL_BG);
  canvas.setTextDatum(middle_center);

  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(COL_PLANE);
  canvas.drawString("DEKLINATION", CX, 38);

  snprintf(buf, sizeof(buf), "%03d", (int)lroundf(heading) % 360);
  canvas.setFont(&fonts::FreeSansBold24pt7b);
  canvas.setTextColor(COL_CARD);
  canvas.drawString(buf, CX, 95);

  int a = abs(declTenths);
  snprintf(buf, sizeof(buf), "Dekl %c%d.%d", declTenths < 0 ? '-' : '+', a / 10, a % 10);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(COL_BUG);
  canvas.drawString(buf, CX, 145);

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("Drehen = +/- 0.1", CX, 185);
  canvas.drawString("Taste = speichern", CX, 207);
  canvas.pushSprite(0, 0);
}

static void drawInfoScreen() {
  char buf[40];
  canvas.fillSprite(COL_BG);
  canvas.setTextDatum(middle_center);

  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(COL_PLANE);
  canvas.drawString(PROJECT_NAME, CX, 46);

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(COL_CARD);
  snprintf(buf, sizeof(buf), "Version %s", PROJECT_VERSION);
  canvas.drawString(buf, CX, 72);
  canvas.drawString(__DATE__, CX, 98);

  const char* sn = (sensorType == SENSOR_QMC) ? "QMC5883L" :
                   (sensorType == SENSOR_HMC) ? "HMC5883L" : "---";
  snprintf(buf, sizeof(buf), "Sensor %s", sn);
  canvas.drawString(buf, CX, 124);

  snprintf(buf, sizeof(buf), "Offset %d  Dekl %.1f", headingOffset, declTenths / 10.0);
  canvas.drawString(buf, CX, 150);

  snprintf(buf, sizeof(buf), "Cal %.0f / %.0f", offX, offY);
  canvas.drawString(buf, CX, 176);

  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("Taste = zurueck", CX, 206);
  canvas.pushSprite(0, 0);
}

static void drawStyleScreen() {
  canvas.fillSprite(COL_BG);
  canvas.setTextDatum(middle_center);

  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(COL_PLANE);
  canvas.drawString("STIL", CX, 38);

  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextColor(COL_BUG);
  const char* names[STYLE_COUNT] = { "Instrument", "Schnaps", "Vertikal" };
  const char* descr[STYLE_COUNT] = { "mit Heading-Bug", "Rose, ohne Bug", "Kartenfenster, ohne Bug" };
  canvas.drawString(names[dispStyle], CX, 100);

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(COL_CARD);
  canvas.drawString(descr[dispStyle], CX, 138);

  // Punkte als Positionsanzeige
  for (int i = 0; i < STYLE_COUNT; i++)
    canvas.fillCircle(CX - 14 + i * 14, 160, 3, i == dispStyle ? COL_BUG : TFT_DARKGREY);

  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("Drehen = wechseln", CX, 187);
  canvas.drawString("Taste = speichern", CX, 207);
  canvas.pushSprite(0, 0);
}

// Schnapskompass: Kompassrose in Messinggehäuse, feste Kurslinie oben, kein Bug
static void drawWhiskey() {
  const uint16_t brass = canvas.color565(181, 138, 60);
  const uint16_t dark  = canvas.color565(40, 40, 40);
  int x0, y0, x1, y1;

  canvas.fillSprite(COL_BG);
  canvas.fillCircle(CX, CY, 120, brass);
  canvas.fillCircle(CX, CY, 114, dark);
  canvas.fillCircle(CX, CY, 110, TFT_BLACK);

  // Rose: Striche
  for (int d = 0; d < 360; d += 5) {
    float ang = d - heading;
    bool major = (d % 10 == 0);
    bool label = (d % 30 == 0);
    polar(108, ang, x0, y0);
    polar(label ? 90 : (major ? 98 : 102), ang, x1, y1);
    if (label) canvas.drawWideLine(x0, y0, x1, y1, 3, COL_CARD);
    else       canvas.drawLine(x0, y0, x1, y1, COL_CARD);
  }

  // Rose: Beschriftung (N, 3, 6, E, 12, 15, S, 21, 24, W, 30, 33)
  canvas.setTextDatum(middle_center);
  for (int d = 0; d < 360; d += 30) {
    float ang = d - heading;
    char t[4];
    bool cardinal = (d % 90 == 0);
    switch (d) {
      case 0:   strcpy(t, "N"); break;
      case 90:  strcpy(t, "E"); break;
      case 180: strcpy(t, "S"); break;
      case 270: strcpy(t, "W"); break;
      default:  snprintf(t, sizeof(t), "%d", d / 10); break;
    }
    canvas.setFont(cardinal ? &fonts::FreeSansBold18pt7b : &fonts::FreeSansBold12pt7b);
    polar(cardinal ? 66 : 70, ang, x0, y0);
    canvas.setTextColor(d == 0 ? COL_N : COL_CARD);
    canvas.drawString(t, x0, y0);
  }

  // Mitte / Dom
  canvas.drawCircle(CX, CY, 30, dark);
  canvas.fillCircle(CX, CY, 4, TFT_DARKGREY);

  // Feste Kurslinie (Lubber Line) oben
  canvas.drawWideLine(CX, 6, CX, 34, 4, TFT_WHITE);
  canvas.pushSprite(0, 0);
}

// Vertikalkartenkompass: waagerecht laufende Kartenanzeige hinter einem Fenster, feste Kurslinie
static void drawVCard() {
  const int WX = 25, WY = 62, WW = 190, WH = 116;
  const uint16_t plate = canvas.color565(28, 28, 28);
  const uint16_t frame = canvas.color565(70, 70, 70);
  const uint16_t screw = canvas.color565(95, 95, 95);

  canvas.fillSprite(plate);

  // Fenster mit Rahmen
  canvas.fillRect(WX - 5, WY - 5, WW + 10, WH + 10, frame);
  canvas.fillRect(WX, WY, WW, WH, TFT_BLACK);

  // Karte nur innerhalb des Fensters zeichnen
  canvas.setClipRect(WX, WY, WW, WH);
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSansBold18pt7b);

  const float sgn = VCARD_HIGHER_LEFT ? -1.0f : 1.0f;
  const int tickTop = 130;

  for (int d = 0; d < 360; d += 5) {
    float dx = fmodf(d - heading + 540.0f, 360.0f) - 180.0f;   // -180..180
    int x = CX + (int)lroundf(sgn * dx * VCARD_PX_PER_DEG);
    if (x < WX - 40 || x > WX + WW + 40) continue;

    bool label = (d % 30 == 0);
    bool major = (d % 10 == 0);
    int len = label ? 26 : (major ? 16 : 9);
    canvas.drawWideLine(x, tickTop, x, tickTop + len, label ? 3 : 2, TFT_WHITE);

    if (label) {
      char t[4];
      switch (d) {
        case 0:   strcpy(t, "N"); break;
        case 90:  strcpy(t, "E"); break;
        case 180: strcpy(t, "S"); break;
        case 270: strcpy(t, "W"); break;
        default:  snprintf(t, sizeof(t), "%d", d / 10); break;
      }
      canvas.setTextColor(d == 0 ? COL_N : TFT_WHITE);
      canvas.drawString(t, x, 96);
    }
  }

  // Feste Kurslinie über die ganze Fensterhöhe
  canvas.drawWideLine(CX, WY, CX, WY + WH, 3, TFT_WHITE);
  canvas.clearClipRect();

  // Schrauben als Zierde
  const int sx[4] = { 48, 192, 48, 192 };
  const int sy[4] = { 40, 40, 200, 200 };
  for (int i = 0; i < 4; i++) {
    canvas.fillCircle(sx[i], sy[i], 7, screw);
    canvas.drawLine(sx[i] - 4, sy[i] + 3, sx[i] + 4, sy[i] - 3, TFT_BLACK);
  }

  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("GS-AVIONIK", CX, 208);
  canvas.pushSprite(0, 0);
}

static void drawCompass() {
  if (dispStyle == STYLE_WHISKEY) { drawWhiskey(); return; }
  if (dispStyle == STYLE_VCARD)   { drawVCard();   return; }
  canvas.fillSprite(COL_BG);
  drawCard();
  drawBug();
  drawPlaneAndReadouts();
  canvas.pushSprite(0, 0);
}

// ======================= Setup / Loop =======================
void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);          // Encoder an, RFID aus
  M5Dial.Display.setBrightness(200);
  M5Dial.Display.fillScreen(COL_BG);

  canvas.setColorDepth(16);
  canvas.createSprite(240, 240);

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  loadCalibration();

  // Startbild
  canvas.fillSprite(COL_BG);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(TFT_GREEN);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.drawString(BOOT_LINE1, CX, CY - 28);
  canvas.drawString(BOOT_LINE2, CX, CY + 4);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.drawString("Version " PROJECT_VERSION, CX, CY + 38);
  canvas.pushSprite(0, 0);
  delay(1500);

  // Sensor suchen
  while (!sensorBegin()) {
    canvas.fillSprite(COL_BG);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(TFT_RED);
    canvas.drawString("Sensor nicht gefunden", CX, CY - 12);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString("GY-271 an Port A?", CX, CY + 14);
    canvas.pushSprite(0, 0);
    delay(1000);
  }

  M5Dial.Encoder.readAndReset();
}

void loop() {
  M5Dial.update();
  updateHeading();                          // Kurs läuft in allen Modi weiter

  int  steps   = encoderSteps();
  bool clicked = M5Dial.BtnA.wasClicked();

  static bool holdHandled = false;
  bool longPress = false;
  if (M5Dial.BtnA.pressedFor(MENU_HOLD_MS) && !holdHandled) {
    holdHandled = true;
    longPress = true;
  }
  if (!M5Dial.BtnA.isPressed()) holdHandled = false;

  switch (mode) {
    case MODE_COMPASS:
      if (dispStyle == STYLE_INSTRUMENT) {      // Bug nur im Instrument-Stil
        if (steps != 0) {
          bugHdg = ((bugHdg + steps * BUG_STEP_DEG) % 360 + 360) % 360;
          bugDirty = true;
          bugChangedAt = millis();
        }
        if (clicked) {
          bugHdg = (int)lroundf(heading) % 360;
          bugDirty = true;
          bugChangedAt = millis();
        }
      }
      if (longPress) {
        menuSel = 0;
        mode = MODE_MENU;
        break;
      }
      drawCompass();
      break;

    case MODE_MENU:
      if (steps != 0) menuSel = ((menuSel + steps) % MENU_COUNT + MENU_COUNT) % MENU_COUNT;
      if (clicked) {
        switch (menuSel) {
          case 0: runCalibration(); mode = MODE_COMPASS; break;
          case 1: mode = MODE_OFFSET; break;
          case 2: mode = MODE_INFO;   break;
          case 3: mode = MODE_DECL;   break;
          case 4: mode = MODE_STYLE;  break;
          case 5: startUpdateMode(); mode = MODE_UPDATE; break;
          default: mode = MODE_COMPASS; break;
        }
        break;
      }
      drawMenu();
      break;

    case MODE_OFFSET:
      if (steps != 0) headingOffset = ((headingOffset + steps) % 360 + 360) % 360;
      if (clicked) {
        saveOffset();
        mode = MODE_COMPASS;
        break;
      }
      drawOffsetScreen();
      break;

    case MODE_DECL:
      if (steps != 0) {
        declTenths += steps;
        if (declTenths > 300)  declTenths = 300;
        if (declTenths < -300) declTenths = -300;
      }
      if (clicked) {
        saveDeclination();
        mode = MODE_COMPASS;
        break;
      }
      drawDeclScreen();
      break;

    case MODE_STYLE:
      if (steps != 0) dispStyle = ((dispStyle + (steps > 0 ? 1 : -1)) % STYLE_COUNT + STYLE_COUNT) % STYLE_COUNT;
      if (clicked) {
        saveStyle();
        mode = MODE_COMPASS;
        break;
      }
      drawStyleScreen();
      break;

    case MODE_UPDATE: {
      server.handleClient();
      if (otaFinished && otaRestartAt && millis() > otaRestartAt) ESP.restart();
      if (clicked && !otaActive && !otaFinished) {
        stopUpdateMode();
        mode = MODE_MENU;
        break;
      }
      static uint32_t lastDraw = 0;
      uint32_t period = otaActive ? 600 : 250;     // beim Upload seltener zeichnen
      if (millis() - lastDraw > period) {
        lastDraw = millis();
        drawUpdateScreen();
      }
      break;
    }

    case MODE_INFO:
      if (clicked) {
        mode = MODE_MENU;
        break;
      }
      drawInfoScreen();
      break;
  }

  // Bug erst nach 2 s Ruhe speichern (schont den Flash)
  if (bugDirty && millis() - bugChangedAt > 2000) {
    saveBug();
    bugDirty = false;
  }

  delay(mode == MODE_UPDATE ? 1 : 10);
}
