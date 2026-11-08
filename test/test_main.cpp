// Unit tests of the pure logic: compass math, calibration, settings limits, UI state machine, rendering.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "BootAnim.h"
#include "Compass.h"
#include "DialUi.h"
#include "Settings.h"

void settingsDefaultCredentials(char* ssid, size_t ssidCap, char* key, size_t keyCap) {
  snprintf(ssid, ssidCap, "GS-MC-E5F6");
  snprintf(key, keyCap, "A1B2C3D4E5F6");
}

static int g_fail = 0, g_n = 0;
#define CHECK(c)                                                        \
  do {                                                                  \
    ++g_n;                                                              \
    if (!(c)) {                                                         \
      ++g_fail;                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);               \
    }                                                                   \
  } while (0)
#define NEAR(a, b, tol) CHECK(fabsf((float)(a) - (float)(b)) <= (tol))

static float angDiff(float a, float b) {
  float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f;
  return fabsf(d);
}

// ---- compass math ------------------------------------------------------------------------------------------------
static void testWrap() {
  NEAR(compass::wrap360(0), 0, 1e-4);
  NEAR(compass::wrap360(360), 0, 1e-4);
  NEAR(compass::wrap360(-1), 359, 1e-4);
  NEAR(compass::wrap360(725), 5, 1e-4);
  NEAR(compass::wrap360(-725), 355, 1e-4);
  NEAR(compass::applyOffsets(300, 40, 180), 124, 1e-3);   // the values used on the bench
  NEAR(compass::applyOffsets(350, 150, 0), 5, 1e-3);      // 350 + 15.0 wraps past north
  NEAR(compass::applyOffsets(10, -30, -20), 347, 1e-3);   // negative values: 10 - 3.0 - 20
}

// field vector of a unit that points to `deg` (sensor frame without mount flags: x = north component, y = east? -> only
// the relation heading = atan2(y, x) matters here)
static void field(float deg, float r, int16_t& x, int16_t& y) {
  x = (int16_t)lroundf(r * cosf(deg * 0.0174533f));
  y = (int16_t)lroundf(r * sinf(deg * 0.0174533f));
}

static void testHeading() {
  compass::Heading h;
  h.configure({false, false, false}, 100);
  for (int d = 0; d < 360; d += 15) {
    int16_t x, y;
    field((float)d, 400, x, y);
    h.reset();
    h.feed(x, y);
    NEAR(angDiff(h.rawDeg(), (float)d), 0, 0.5f);
  }
  // mounting flags
  h.configure({true, false, false}, 100);  // swap: x' = y, y' = x
  h.reset(); h.feed(0, 100);               // y only -> x' = 100 -> 0 degrees
  NEAR(angDiff(h.rawDeg(), 0), 0, 0.5f);
  h.configure({false, true, false}, 100);
  h.reset(); h.feed(100, 0);
  NEAR(angDiff(h.rawDeg(), 180), 0, 0.5f);
  h.configure({false, false, true}, 100);
  h.reset(); h.feed(0, 100);
  NEAR(angDiff(h.rawDeg(), 270), 0, 0.5f);
  // the bench setup: swap + invert X
  h.configure({true, true, false}, 100);
  h.reset(); h.feed(0, 100);  // x' = y = 100 -> inverted -> -100, y' = 0
  NEAR(angDiff(h.rawDeg(), 180), 0, 0.5f);
  // calibration is applied before the mounting
  h.configure({false, false, false}, 100);
  h.setCal({10, 20, 2, 1, true});
  h.reset(); h.feed(10, 120);  // (0, 100) after the calibration -> 90 degrees
  NEAR(angDiff(h.rawDeg(), 90), 0, 0.5f);
}

static void testFilter() {
  compass::Heading h;
  h.configure({false, false, false}, 20);
  int16_t x, y;
  field(359, 400, x, y);
  h.feed(x, y);
  // jump to 1 degree: no detour through 180
  float prev = h.rawDeg();
  for (int i = 0; i < 40; ++i) {
    field(1, 400, x, y);
    h.feed(x, y);
    float cur = h.rawDeg();
    CHECK(angDiff(cur, prev) < 3.0f);
    prev = cur;
  }
  NEAR(angDiff(h.rawDeg(), 1), 0, 0.5f);
  // smoothing limits
  h.configure({false, false, false}, 0);    // clamped to 5 %
  h.reset(); field(0, 400, x, y); h.feed(x, y); field(90, 400, x, y); h.feed(x, y);
  CHECK(angDiff(h.rawDeg(), 0) < 10.0f);
  CHECK(!h.hasData() || h.hasData());
}

static void testCalMath() {
  compass::Calibrator c;
  c.begin();
  compass::Cal out = compass::defaultCal();
  CHECK(!c.finish(out));  // no data
  // ellipse: centre (120, -80), radius 500 in x, 250 in y
  for (int d = 0; d < 360; d += 3) {
    c.feed((int16_t)lroundf(120 + 500 * cosf(d * 0.0174533f)), (int16_t)lroundf(-80 + 250 * sinf(d * 0.0174533f)));
  }
  CHECK(c.finish(out));
  NEAR(out.offX, 120, 2);
  NEAR(out.offY, -80, 2);
  NEAR(out.scX * c.spanX(), out.scY * c.spanY(), 1.0f);  // both axes get the same span
  CHECK(out.scX < 1.0f && out.scY > 1.0f);
  CHECK(out.valid);
  // after the correction every direction gives the right angle
  compass::Heading h;
  h.configure({false, false, false}, 100);
  h.setCal(out);
  for (int d = 0; d < 360; d += 20) {
    h.reset();
    h.feed((int16_t)lroundf(120 + 500 * cosf(d * 0.0174533f)), (int16_t)lroundf(-80 + 250 * sinf(d * 0.0174533f)));
    NEAR(angDiff(h.rawDeg(), (float)d), 0, 1.5f);
  }
  // too little movement is rejected and leaves `out` alone
  compass::Calibrator s;
  s.begin();
  for (int i = 0; i < 100; ++i) s.feed((int16_t)(i % 5), (int16_t)(i % 7));
  compass::Cal keep = out;
  CHECK(!s.finish(keep));
  NEAR(keep.offX, out.offX, 1e-6);
  // one axis without movement is rejected as well
  compass::Calibrator t;
  t.begin();
  for (int d = 0; d < 360; d += 5) t.feed((int16_t)lroundf(300 * cosf(d * 0.0174533f)), 3);
  CHECK(!t.finish(keep));
  // extreme ratio is rejected
  compass::Calibrator u;
  u.begin();
  for (int d = 0; d < 360; d += 5) u.feed((int16_t)lroundf(3000 * cosf(d * 0.0174533f)), (int16_t)lroundf(100 * sinf(d * 0.0174533f)));
  CHECK(!u.finish(keep));
}

static void testState() {
  compass::State s;
  CHECK(!s.fresh(100));
  s.lastRxMs = 100;
  CHECK(s.fresh(900));
  CHECK(!s.fresh(1200));
  CHECK(s.fresh(95));  // a sample taken after the clock was read is fresh, not 4 billion ms old
  CHECK(compass::State::age(100, 105) == 0 && compass::State::age(105, 100) == 5);
  s.lastRxMs = 0xFFFFFFF0u;
  CHECK(s.fresh(0x10u));  // millis() wrap-around
  CHECK(strcmp(compass::sensorName(compass::kSensorQmc), "QMC5883L") == 0);
  CHECK(strcmp(compass::sensorName(compass::kSensorHmc), "HMC5883L") == 0);
  CHECK(strcmp(compass::sensorName(compass::kSensorNone), "none") == 0);
}

// ---- settings ----------------------------------------------------------------------------------------------------
static void testSettings() {
  Settings s;
  settingsDefaults(s);
  CHECK(strcmp(s.ssid, "GS-MC-E5F6") == 0);
  CHECK(strcmp(s.wpakey, "A1B2C3D4E5F6") == 0);
  CHECK(!s.wifiBoot);
  CHECK(s.dialBright == 80 && s.style == 0 && s.startPage == 0);
  CHECK(s.offsetDeg == 180 && s.declTenths == 40 && s.smooth == 15);
  CHECK(s.swapXY && s.invX && !s.invY);
  Settings t = s;
  settingsSanitize(t);
  CHECK(memcmp(&s, &t, sizeof s) == 0);  // defaults are valid and unchanged by sanitize
  // limits
  s.dialBright = 3; s.style = 9; s.startPage = 7; s.bug = 725; s.declTenths = 999; s.smooth = 0; s.debug = 9;
  settingsSanitize(s);
  CHECK(s.dialBright == 10 && s.style == 2 && s.startPage == 2 && s.bug == 5 && s.declTenths == 300 && s.smooth == 5 && s.debug == 2);
  s.dialBright = 200; s.declTenths = -999;
  settingsSanitize(s);
  CHECK(s.dialBright == 100 && s.declTenths == -300);
  // offset: 0..359 from firmware 1.x becomes -180..180
  int in[]  = {0, 90, 180, 181, 270, 359, 360, -90, -180, -181, 540};
  int out[] = {0, 90, 180, -179, -90, -1, 0, -90, -180, 179, 180};
  for (unsigned i = 0; i < sizeof in / sizeof in[0]; ++i) {
    settingsDefaults(s);
    s.offsetDeg = (int16_t)in[i];
    settingsSanitize(s);
    if (s.offsetDeg != out[i]) printf("offset %d -> %d (expected %d)\n", in[i], s.offsetDeg, out[i]);
    CHECK(s.offsetDeg == out[i]);
  }
  // credentials
  settingsDefaults(s);
  s.ssid[0] = 0; strcpy(s.wpakey, "short");
  settingsSanitize(s);
  CHECK(strcmp(s.ssid, "GS-MC-E5F6") == 0 && strcmp(s.wpakey, "A1B2C3D4E5F6") == 0);
  // unterminated strings
  memset(s.ssid, 'a', sizeof s.ssid);
  memset(s.wpakey, 'b', sizeof s.wpakey);
  settingsSanitize(s);
  CHECK(strlen(s.ssid) == 32 && strlen(s.wpakey) == 63);
}

// ---- UI ----------------------------------------------------------------------------------------------------------
static DialEnv env;
static uint16_t band[240 * 30];

static void renderFrame(DialUi& ui, uint32_t now) {
  gfx::Canvas c;
  for (int y0 = 0; y0 < 240; y0 += 30) {
    c.setBand(band, y0, 30);
    ui.render(c, env, now);
  }
}

static void resetEnv(uint32_t now) {
  env = DialEnv();
  strcpy(env.ip, "192.168.1.1");
  strcpy(env.ssid, "GS-MC-E5F6");
  strcpy(env.key, "A1B2C3D4E5F6");
  env.fw = "2.0";
  env.hw = "GS-Micro-Compass";
  env.comp.sensor = compass::kSensorQmc;
  env.comp.lastRxMs = now;
}

static Settings defaultsWithStyle(int style) {
  Settings s;
  settingsDefaults(s);
  s.style = (uint8_t)style;
  return s;
}

static void testPages() {
  resetEnv(0);
  DialUi ui;
  ui.begin(defaultsWithStyle(1), 0);  // whiskey style
  CHECK(ui.page() == DialUi::kCompass);
  // the compass is the home page: wheel and click do nothing there
  ui.onRotate(1); ui.onRotate(-1); ui.onClick();
    CHECK(ui.page() == DialUi::kCompass);
  // a long press leads into the menu pages, starting at Settings (closed)
  ui.onLongPress();
  CHECK(ui.page() == DialUi::kMenu && !ui.menuOpen());
  // endless ring: WiFi, Settings, Info
  ui.onRotate(1);  CHECK(ui.page() == DialUi::kInfo);
  ui.onRotate(1);  CHECK(ui.page() == DialUi::kWifi);   // the compass is not part of the ring
  ui.onRotate(1);  CHECK(ui.page() == DialUi::kMenu);
  ui.onRotate(-1); CHECK(ui.page() == DialUi::kWifi);
  ui.onRotate(-1); CHECK(ui.page() == DialUi::kInfo);
  ui.onRotate(-1); CHECK(ui.page() == DialUi::kMenu);
  // a long press goes back to the compass from every page
  for (int p = DialUi::kWifi; p <= DialUi::kInfo; ++p) {
    ui.gotoPage(p); ui.onLongPress();
    CHECK(ui.page() == DialUi::kCompass);
  }
  // no beep for a wheel turn that does nothing
  DialUi q; q.begin(defaultsWithStyle(2), 0); q.takeBeep();
  q.onRotate(1); CHECK(!q.takeBeep());
  // start page setting
  Settings s = defaultsWithStyle(0);
  s.startPage = 1; { DialUi u; u.begin(s, 0); CHECK(u.page() == DialUi::kWifi); }
  s.startPage = 2; { DialUi u; u.begin(s, 0); CHECK(u.page() == DialUi::kInfo); }
  s.startPage = 0; { DialUi u; u.begin(s, 0); CHECK(u.page() == DialUi::kCompass); }
}

static void testBug() {
  resetEnv(0);
  Settings s = defaultsWithStyle(0);
  s.bug = 10;
  DialUi ui;
  ui.begin(s, 0);
  // instrument style: the wheel sets the bug, the page stays
  ui.onRotate(1);  CHECK(ui.settings().bug == 11 && ui.page() == DialUi::kCompass);
  ui.onRotate(-1); CHECK(ui.settings().bug == 10);
  ui.onRotate(-1); ui.onRotate(-10); // fast spin arrives as a batch: x15
  CHECK(ui.settings().bug == ((10 - 1 - 150) % 360 + 360) % 360);
  { DialUi f; Settings t = defaultsWithStyle(0); t.bug = 100; f.begin(t, 0);
    f.onRotate(2); CHECK(f.settings().bug == 130);   // 2 clicks in one frame = 30 degrees
    f.onRotate(1); CHECK(f.settings().bug == 131);   // a single click stays at 1 degree
    f.onRotate(-3); CHECK(f.settings().bug == 86); }
  ui.onRotate(400); CHECK(ui.settings().bug < 360);
  // wraps at 0 / 359
  { DialUi u; Settings t = defaultsWithStyle(0); t.bug = 0; u.begin(t, 0); u.onRotate(-1); CHECK(u.settings().bug == 359); u.onRotate(1); CHECK(u.settings().bug == 0); }
  // stored two seconds after the last turn, once
  DialUi v;
  Settings t = defaultsWithStyle(0);
  v.begin(t, 0);
  renderFrame(v, 3000);
  v.onRotate(1);
  renderFrame(v, 3500);
  CHECK(v.takeAction() == kDialNone);
  renderFrame(v, 5600);
  CHECK(v.takeAction() == kDialSave);
  renderFrame(v, 5800);
  CHECK(v.takeAction() == kDialNone);
  // click: the bug jumps to the current heading (300 + 4.0 + 180 = 124), stored two seconds later
  DialUi w; w.begin(defaultsWithStyle(0), 0);
  env.comp.rawDeg = 300.0f;
  renderFrame(w, 3000);
  w.onClick();
  CHECK(w.settings().bug == 124 && w.page() == DialUi::kCompass);
  renderFrame(w, 5200);
  CHECK(w.takeAction() == kDialSave);
  // the other styles have no bug: wheel and click change nothing
  for (int st = 1; st <= 2; ++st) {
    DialUi x; x.begin(defaultsWithStyle(st), 0);
    renderFrame(x, 3000);
    unsigned b = x.settings().bug;
    x.onRotate(1); x.onClick();
    CHECK(x.settings().bug == b && x.page() == DialUi::kCompass);
  }
  // the menu pages are reached with a long press and turned with the wheel
  DialUi y; y.begin(defaultsWithStyle(0), 0);
  y.onLongPress(); CHECK(y.page() == DialUi::kMenu);
  y.onRotate(1); CHECK(y.page() == DialUi::kInfo);
  y.onLongPress(); CHECK(y.page() == DialUi::kCompass);
}

static void testMenu() {
  resetEnv(0);
  DialUi ui;
  ui.begin(defaultsWithStyle(1), 0);
  ui.gotoPage(DialUi::kMenu);
  CHECK(!ui.menuOpen());
  ui.onRotate(1);  // closed Settings page: turning goes on to Info
  CHECK(ui.page() == DialUi::kInfo);
  ui.gotoPage(DialUi::kMenu);
  ui.onClick();
  CHECK(ui.menuOpen() && ui.menuCategory() == -1 && ui.menuIndex() == 0);
  // endless over Display, Compass, System, Back
  ui.onRotate(-1); CHECK(ui.menuIndex() == 3);
  ui.onRotate(1);  CHECK(ui.menuIndex() == 0);
  for (int i = 0; i < 4; ++i) ui.onRotate(1);
  CHECK(ui.menuIndex() == 0);
  // Display > Brightness: edit, clamp, no wrap
  ui.onClick(); CHECK(ui.menuCategory() == 0 && ui.menuIndex() == 0);
  ui.onClick(); CHECK(ui.editing());
  ui.onRotate(-3); CHECK(ui.settings().dialBright == 50);
  ui.onRotate(-20); CHECK(ui.settings().dialBright == 10);
  ui.onRotate(30); CHECK(ui.settings().dialBright == 100);
  ui.onClick(); CHECK(!ui.editing());
  // Style: 0..2, clamped
  ui.onRotate(1); ui.onClick(); CHECK(ui.editing());
  ui.onRotate(1); CHECK(ui.settings().style == 2);
  ui.onRotate(1); CHECK(ui.settings().style == 2);
  ui.onRotate(-1); ui.onRotate(-1); ui.onRotate(-1); CHECK(ui.settings().style == 0);
  ui.onRotate(1); CHECK(ui.settings().style == 1);
  ui.onClick();
  // Start page: 0..2
  ui.onRotate(1); ui.onClick(); ui.onRotate(5); CHECK(ui.settings().startPage == 1); ui.onRotate(1); CHECK(ui.settings().startPage == 2); ui.onRotate(1); CHECK(ui.settings().startPage == 2); ui.onClick();
  // Back row returns to the category list, cursor on Display
  ui.onRotate(1); CHECK(ui.menuIndex() == 3);
  ui.onClick(); CHECK(ui.menuCategory() == -1 && ui.menuIndex() == 0);
  // Back on the top level leaves and saves without a restart
  ui.onRotate(-1); ui.onClick();
  CHECK(!ui.menuOpen());
  CHECK(ui.takeAction() == kDialSave);
  // leaving without changes saves nothing
  ui.onClick(); ui.onRotate(-1); ui.onClick();
  CHECK(ui.takeAction() == kDialNone);
  // long press leaves the menu and saves
  DialUi lp; lp.begin(defaultsWithStyle(1), 0);
  lp.gotoPage(DialUi::kMenu); lp.onClick(); lp.onClick(); lp.onClick(); lp.onRotate(-1);
  lp.onLongPress();
  CHECK(lp.page() == DialUi::kCompass && !lp.menuOpen());
  CHECK(lp.takeAction() == kDialSave);
}

static void testCompassMenu() {
  resetEnv(0);
  DialUi ui;
  ui.begin(defaultsWithStyle(1), 0);
  ui.gotoPage(DialUi::kMenu); ui.onClick();
  ui.onRotate(1); ui.onClick();  // Compass
  CHECK(ui.menuCategory() == 1);
  ui.onRotate(1); ui.onClick(); CHECK(ui.editing());  // Offset
  ui.onRotate(1); CHECK(ui.settings().offsetDeg == 180);  // clamped at +180
  ui.onRotate(-1); CHECK(ui.settings().offsetDeg == 179);
  ui.onRotate(-4); CHECK(ui.settings().offsetDeg == 159);  // fast: x5
  ui.onRotate(-100); CHECK(ui.settings().offsetDeg == -180);
  ui.onRotate(-1); CHECK(ui.settings().offsetDeg == -180);
  ui.onClick();
  ui.onRotate(1); ui.onClick();  // Declination
  ui.onRotate(1); CHECK(ui.settings().declTenths == 41);
  ui.onRotate(-3); CHECK(ui.settings().declTenths == 26);
  ui.onRotate(-200); CHECK(ui.settings().declTenths == -300);
  ui.onRotate(1); CHECK(ui.settings().declTenths == -299);
  ui.onClick();
  // live heading follows the working copy: offset 0, declination 0 -> raw heading
  ui.onRotate(-1); ui.onClick(); ui.onRotate(-1000); ui.onRotate(1000);
  env.comp.rawDeg = 100.0f;
  ui.onClick();
  renderFrame(ui, 5000);  // must not crash
  ui.onLongPress();
  CHECK(ui.takeAction() == kDialSave);
}

static void testSystem() {
  resetEnv(0);
  DialUi ui;
  ui.begin(defaultsWithStyle(1), 0);
  ui.gotoPage(DialUi::kMenu); ui.onClick();
  ui.onRotate(1); ui.onRotate(1); ui.onClick();  // System
  CHECK(ui.menuCategory() == 2);
  // WiFi at boot toggles, saved on leaving
  ui.onClick(); CHECK(ui.settings().wifiBoot);
  ui.onClick(); CHECK(!ui.settings().wifiBoot);
  ui.onClick();
  // Restart: save and restart
  ui.onRotate(1); ui.onClick();
  CHECK(ui.takeAction() == kDialSaveReboot);
  renderFrame(ui, 5000);
  ui.onClick(); ui.onRotate(1);
  CHECK(ui.takeAction() == kDialNone);  // input ignored while restarting
  // Factory reset: confirmation, Cancel preselected
  DialUi f; f.begin(defaultsWithStyle(1), 0);
  f.gotoPage(DialUi::kMenu); f.onClick();
  f.onRotate(1); f.onRotate(1); f.onClick();
  f.onRotate(1); f.onRotate(1); f.onClick();
  CHECK(f.confirmState() == 1);
  f.onClick();  // confirm Cancel
  CHECK(f.confirmState() == 0 && f.takeAction() == kDialNone);
  f.onClick(); CHECK(f.confirmState() == 1);
  f.onRotate(1); CHECK(f.confirmState() == 2);
  f.onRotate(-1); CHECK(f.confirmState() == 1);
  f.onRotate(1); f.onClick();
  CHECK(f.takeAction() == kDialFactoryReset);
  renderFrame(f, 5000);
}

static void testWifi() {
  resetEnv(0);
  DialUi ui;
  ui.begin(defaultsWithStyle(1), 0);
  renderFrame(ui, 3000);
  ui.gotoPage(DialUi::kWifi);
  env.wifiOn = false;
  renderFrame(ui, 3000);
  ui.onClick(); CHECK(ui.takeAction() == kDialWifiOn);
  env.wifiOn = true;
  renderFrame(ui, 3100);
  ui.onClick(); CHECK(ui.takeAction() == kDialWifiOff);
  // never switch off during an upload
  env.ota = true; env.otaKb = 120;
  renderFrame(ui, 3200);
  ui.onClick(); CHECK(ui.takeAction() == kDialNone);
  env.ota = false;
}

static void testCalibration() {
  resetEnv(0);
  DialUi ui;
  ui.begin(defaultsWithStyle(1), 0);
  uint32_t now = 3000;
  ui.gotoPage(DialUi::kMenu); ui.onClick();
  ui.onRotate(1); ui.onClick();  // Compass
  CHECK(ui.fastRedraw());
  ui.onClick();                  // Calibrate
  CHECK(ui.calState() == 1 && ui.takeAction() == kDialCalStart);
  // the wheel is not used while calibrating
  ui.onRotate(1); CHECK(ui.page() == DialUi::kMenu);
  // before loop() reports it, click does not stop anything
  ui.onClick(); CHECK(ui.takeAction() == kDialNone);
  env.comp.calActive = true; env.comp.calRemainingMs = 15000; env.comp.calProgX = 40; env.comp.calProgY = 10;
  renderFrame(ui, now);
  ui.onClick(); CHECK(ui.takeAction() == kDialCalStop);
  // loop() finishes: result screen, then back to the menu
  env.comp.calActive = false; env.comp.calResult = 1;
  renderFrame(ui, now + 100);
  CHECK(ui.calState() == 2);
  renderFrame(ui, now + 3000);
  CHECK(ui.calState() == 0);
  // long press cancels
  ui.onClick(); CHECK(ui.calState() == 1); ui.takeAction();
  env.comp.calActive = true; env.comp.calResult = 0;
  renderFrame(ui, now + 4000);
  ui.onLongPress();
  CHECK(ui.calState() == 0 && ui.takeAction() == kDialCalCancel);
  // loop() never reports activity (no sensor): the UI shows "Failed" after 1.5 s
  env.comp.calActive = false;
  DialUi n; n.begin(defaultsWithStyle(1), 0);
  n.gotoPage(DialUi::kMenu); renderFrame(n, 9990);  // the UI learns the time from the frames
  n.onClick(); n.onRotate(1); n.onClick(); n.onClick();
  renderFrame(n, 10000);
  CHECK(n.calState() == 1);
  renderFrame(n, 11700);
  CHECK(n.calState() == 2);
  renderFrame(n, 14500);
  CHECK(n.calState() == 0);
}

static int countColor(DialUi& ui, uint32_t now, uint16_t col) {
  const uint16_t swapped = (uint16_t)((col << 8) | (col >> 8));
  int n = 0;
  gfx::Canvas c;
  for (int y0 = 0; y0 < 240; y0 += 30) {
    c.setBand(band, y0, 30);
    ui.render(c, env, now);
    for (int i = 0; i < 240 * 30; ++i) if (band[i] == col || band[i] == swapped) ++n;
  }
  return n;
}

static void testNoDataBox() {
  resetEnv(10000);
  for (int st = 0; st < 3; ++st) {
    DialUi ui;
    ui.begin(defaultsWithStyle(st), 0);
    env.comp.lastRxMs = 10000;
    CHECK(countColor(ui, 10000, gfx::kRed) == 0);   // fresh
    env.comp.lastRxMs = 10006;                      // sample arrived after the frame time was read
    CHECK(countColor(ui, 10000, gfx::kRed) == 0);
    env.comp.lastRxMs = 8000;                       // really stale
    CHECK(countColor(ui, 10000, gfx::kRed) > 0);
    env.comp.sensor = compass::kSensorNone;
    CHECK(countColor(ui, 10000, gfx::kRed) > 0);
    env.comp.sensor = compass::kSensorQmc;
  }
}

static int countRgb(DialUi& ui, uint32_t now, uint16_t col) { return countColor(ui, now, col); }

static void testBootAnim() {
  resetEnv(0);
  // every frame of the animation renders (also far beyond the end) and the frames really change
  gfx::Canvas c;
  static uint16_t first[240 * 30], other[240 * 30];
  bool changed = false;
  for (uint32_t t = 0; t <= bootanim::kDurationMs + 500; t += 50) {
    c.setBand(band, 90, 30);
    bootanim::draw(c, t, "GS Avionik", "Product", "v1.0");
    if (t == 0) memcpy(first, band, sizeof first);
    if (t == 1000) memcpy(other, band, sizeof other);
  }
  changed = memcmp(first, other, sizeof first) != 0;
  CHECK(changed);
  // before the start there is nothing, at the end the three lines are readable at the template positions
  DialUi ui; ui.begin(defaultsWithStyle(0), 0);
  CHECK(countRgb(ui, 0, gfx::kGreen) == 0);
  CHECK(countRgb(ui, 2400, gfx::kGreen) > 500);   // ring, ticks and the title
  CHECK(countRgb(ui, 2400, gfx::kWhite) > 100);   // product name
  // the same final frame as the old static boot screen: texts only inside the ring
  // after the animation the compass appears
  CHECK(countRgb(ui, 2600, gfx::kYellow) > 0);
  // a click skips the animation, a wheel turn is ignored while it runs
  DialUi k; k.begin(defaultsWithStyle(0), 0);
  unsigned bug = k.settings().bug;
  renderFrame(k, 300);
  k.onRotate(1);
  CHECK(k.settings().bug == bug);              // ignored during the animation
  k.onClick();
  CHECK(k.takeAction() == kDialNone && k.settings().bug == bug);  // the click only skipped
  CHECK(countRgb(k, 400, gfx::kYellow) > 0);   // compass already visible
  k.onRotate(1);
  CHECK(k.settings().bug == bug + 1);          // now the wheel works
  // a long press skips as well and does not open the menu
  DialUi l; l.begin(defaultsWithStyle(0), 0);
  renderFrame(l, 300);
  l.onLongPress();
  renderFrame(l, 350);
  CHECK(l.page() == DialUi::kCompass);
  l.onLongPress(); CHECK(l.page() == DialUi::kMenu);
}

static void testBootRedrawRate() {
  resetEnv(0);
  // with the start page on Info (not the compass) the animation must still be redrawn quickly
  Settings st = defaultsWithStyle(0);
  st.startPage = 2;
  DialUi ui; ui.begin(st, 0);
  CHECK(ui.page() == DialUi::kInfo);
  renderFrame(ui, 100);
  CHECK(ui.fastRedraw());           // animation running
  renderFrame(ui, 2400);
  CHECK(ui.fastRedraw());
  renderFrame(ui, 2600);
  CHECK(!ui.fastRedraw());          // finished: the info page is static again
  // the compass keeps the fast rate
  DialUi c; c.begin(defaultsWithStyle(0), 0); renderFrame(c, 2600);
  CHECK(c.fastRedraw());
}

static void testRenderAll() {
  resetEnv(10000);
  for (int st = 0; st < 3; ++st) {
    DialUi ui;
    ui.begin(defaultsWithStyle(st), 0);
    for (float raw = 0; raw < 360; raw += 37.0f) {
      env.comp.rawDeg = raw;
      renderFrame(ui, 10000);
    }
    env.comp.sensor = compass::kSensorNone;
    renderFrame(ui, 10000);
    env.comp.sensor = compass::kSensorQmc;
    env.comp.lastRxMs = 1;  // stale
    renderFrame(ui, 10000);
    env.comp.lastRxMs = 10000;
    for (int p = 0; p < DialUi::kPageCount; ++p) { ui.gotoPage(p); renderFrame(ui, 10000); }
  }
  // boot animation within the first 2.5 s; its very first frame (t = 0) is still completely black
  DialUi ui; ui.begin(defaultsWithStyle(0), 100); renderFrame(ui, 150);
  gfx::Canvas c; c.setBand(band, 0, 30); ui.render(c, env, 100);
  bool allBlack = true; for (int i = 0; i < 240 * 30; ++i) if (band[i] != gfx::kBlack) allBlack = false;
  CHECK(allBlack);
}

int main() {
  testWrap();
  testHeading();
  testFilter();
  testCalMath();
  testState();
  testSettings();
  testPages();
  testBug();
  testMenu();
  testCompassMenu();
  testSystem();
  testWifi();
  testCalibration();
  testNoDataBox();
  testBootAnim();
  testBootRedrawRate();
  testRenderAll();
  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}

