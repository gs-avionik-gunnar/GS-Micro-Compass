#include "DialUi.h"

#include "BootAnim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace gfx;

static const uint16_t kBrass = rgb(181, 138, 60);
static const uint16_t kPlate = rgb(28, 28, 28);
static const uint16_t kFrame = rgb(70, 70, 70);
static const uint16_t kScrew = rgb(95, 95, 95);

// ---- small helpers ----------------------------------------------------------------------------------------------
// Point at radius r around the display centre; deg = 0 is up, clockwise.
static void polar(float r, float deg, int& x, int& y) {
  float a = deg * 0.01745329f;
  x = 120 + (int)lroundf(r * sinf(a));
  y = 120 - (int)lroundf(r * cosf(a));
}

// Text centred on (cx, cy); digits and capitals are centred vertically.
static void ctext(Canvas& c, const DialFont& f, int cx, int cy, uint16_t col, const char* s) {
  c.text(f, cx, cy - (f.ascent * 5) / 8, Center, col, s);
}

static const char* labelFor(int deg, char* buf, size_t n) {
  switch (deg) {
    case 0: return "N";
    case 90: return "E";
    case 180: return "S";
    case 270: return "W";
    default: snprintf(buf, n, "%d", deg / 10); return buf;
  }
}

// A fast spin arrives as a batch of several clicks per frame.
static int stepScale(int steps) { return (steps >= 2 || steps <= -2) ? 5 : 1; }      // menu values
static int bugScale(int steps) { return (steps >= 2 || steps <= -2) ? 15 : 1; }     // heading bug

static int clampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- state -------------------------------------------------------------------------------------------------------
void DialUi::begin(const Settings& s, uint32_t now) {
  set_ = s;
  orig_ = s;
  t0_ = now;
  now_ = now;
  static const uint8_t kStartMap[3] = {kCompass, kWifi, kInfo};
  page_ = s.startPage < 3 ? (int)kStartMap[s.startPage] : (int)kCompass;
  dirty_ = true;
}

const uint8_t* DialUi::catItems(int cat, int& len) {
  static const uint8_t kDisplayItems[] = {kMiBright, kMiStyle, kMiStart, kMiBack};
  static const uint8_t kCompassItems[] = {kMiCalibrate, kMiOffset, kMiDecl, kMiBack};
  static const uint8_t kSysItems[] = {kMiWifiBoot, kMiRestart, kMiReset, kMiBack};
  switch (cat) {
    case 0: len = 4; return kDisplayItems;
    case 1: len = 4; return kCompassItems;
    default: len = 4; return kSysItems;
  }
}

int DialUi::listLen() const {
  if (menuCat_ < 0) return kCats + 1;  // categories + Back
  int n;
  catItems(menuCat_, n);
  return n;
}

int DialUi::curItem() const {
  if (menuCat_ < 0) return -1;
  int n;
  const uint8_t* it = catItems(menuCat_, n);
  return menuSel_ < n ? (int)it[menuSel_] : (int)kMiBack;
}

bool DialUi::fastRedraw() const {
  if (splashActive()) return true;  // the boot animation needs ~25 frames per second on every start page
  if (rebooting_ || confirm_) return false;
  return page_ == kCompass || calUi_ != 0 || (page_ == kMenu && menuOpen_ && menuCat_ == 1);
}

float DialUi::heading(const DialEnv& e) const { return compass::applyOffsets(e.comp.rawDeg, set_.declTenths, set_.offsetDeg); }

bool DialUi::splashActive() const { return splashShown_ && compass::State::age(now_, t0_) < bootanim::kDurationMs; }

void DialUi::skipSplash() {
  t0_ = now_ - bootanim::kDurationMs;  // ends the animation with the next frame
  splashShown_ = false;
  dirty_ = true;
}

void DialUi::onRotate(int steps) {
  if (steps == 0 || rebooting_ || splashActive()) return;
  if (page_ == kCompass && !confirm_ && !calUi_ && set_.style != kStyleInstrument) return;  // nothing to turn on this compass
  beep_ = true;
  dirty_ = true;
  if (confirm_) {
    confirm_ = (steps > 0) ? 2 : 1;
    return;
  }
  if (calUi_) return;  // the wheel is not used while calibrating
  if (page_ == kCompass) {
    // instrument style: the wheel sets the heading bug (a long press opens the menu)
    int d = steps * bugScale(steps);
    set_.bug = (uint16_t)(((int)set_.bug + d) % 360 + 360) % 360;
    bugDirty_ = true;
    bugT_ = now_;
    return;
  }
  if (page_ == kMenu && menuOpen_) {
    if (editing_) {
      const int sc = stepScale(steps);
      switch (curItem()) {
        case kMiBright: set_.dialBright = (uint8_t)clampI((int)set_.dialBright + steps * 10, 10, 100); break;
        case kMiStyle: set_.style = (uint8_t)clampI((int)set_.style + (steps > 0 ? 1 : -1), 0, kStyleCount - 1); break;
        case kMiStart: set_.startPage = (uint8_t)clampI((int)set_.startPage + (steps > 0 ? 1 : -1), 0, 2); break;
        case kMiOffset: set_.offsetDeg = (int16_t)clampI((int)set_.offsetDeg + steps * sc, -180, 180); break;
        case kMiDecl: set_.declTenths = (int16_t)clampI((int)set_.declTenths + steps * sc, -300, 300); break;
        default: break;
      }
      return;
    }
    const int len = listLen();  // endless: after the last row comes the first one and vice versa
    menuSel_ = (menuSel_ + (steps > 0 ? 1 : len - 1)) % len;
    if (menuSel_ < menuTop_) menuTop_ = menuSel_;
    if (menuSel_ > menuTop_ + 4) menuTop_ = menuSel_ - 4;
    return;
  }
  // the menu pages WiFi, Settings and Info form an endless ring; the compass is the home page outside of it
  const int idx = page_ - kWifi, n = kPageCount - 1;
  page_ = kWifi + (idx + (steps > 0 ? 1 : n - 1)) % n;
}

void DialUi::leaveMenu() {
  editing_ = false;
  menuOpen_ = false;  // back on the closed Settings page
  menuCat_ = -1;
  bool changed = set_.dialBright != orig_.dialBright || set_.style != orig_.style || set_.startPage != orig_.startPage ||
                 set_.offsetDeg != orig_.offsetDeg || set_.declTenths != orig_.declTenths || set_.wifiBoot != orig_.wifiBoot ||
                 set_.bug != orig_.bug;
  if (changed) {
    action_ = kDialSave;  // nothing in the menu needs a restart
    orig_ = set_;
    bugDirty_ = false;
  }
}

void DialUi::onClick() {
  if (rebooting_) return;
  if (splashActive()) {
    skipSplash();
    return;
  }
  beep_ = true;
  dirty_ = true;
  if (confirm_) {
    if (confirm_ == 2) {
      action_ = kDialFactoryReset;
      rebooting_ = true;
    }
    confirm_ = 0;
    return;
  }
  if (calUi_) {
    if (calUi_ == 1 && calSeen_) action_ = kDialCalStop;  // finish now
    else if (calUi_ == 2) calUi_ = 0;                      // close the result
    return;
  }
  if (page_ == kCompass) {
    if (set_.style == kStyleInstrument) {  // click = heading bug to the current heading
      set_.bug = (uint16_t)(((int)lroundf(lastHeading_) % 360 + 360) % 360);
      bugDirty_ = true;
      bugT_ = now_;
    }
    return;
  }
  if (page_ == kWifi) {
    if (otaActive_) return;  // never switch off during a firmware upload
    action_ = wifiOn_ ? kDialWifiOff : kDialWifiOn;
    return;
  }
  if (page_ != kMenu) return;
  if (!menuOpen_) {
    menuOpen_ = true;
    menuCat_ = -1;
    menuSel_ = 0;
    menuTop_ = 0;
    return;
  }
  if (editing_) {
    editing_ = false;
    return;
  }
  if (menuCat_ < 0) {  // top level: open a category, or leave
    if (menuSel_ >= kCats) {
      leaveMenu();
    } else {
      menuCat_ = menuSel_;
      menuSel_ = 0;
      menuTop_ = 0;
    }
    return;
  }
  switch (curItem()) {
    case kMiBright:
    case kMiStyle:
    case kMiStart:
    case kMiOffset:
    case kMiDecl: editing_ = true; break;
    case kMiWifiBoot: set_.wifiBoot = !set_.wifiBoot; break;
    case kMiCalibrate:
      calUi_ = 1;
      calSeen_ = false;
      calT_ = now_;
      action_ = kDialCalStart;
      break;
    case kMiRestart:  // saves pending changes and restarts
      action_ = kDialSaveReboot;
      rebooting_ = true;
      break;
    case kMiReset: confirm_ = 1; break;
    case kMiBack:  // back to the category list
      menuSel_ = menuCat_;
      menuCat_ = -1;
      menuTop_ = 0;
      break;
  }
}

void DialUi::onLongPress() {
  if (rebooting_) return;
  if (splashActive()) {
    skipSplash();
    return;
  }
  beep_ = true;
  dirty_ = true;
  confirm_ = 0;
  if (calUi_) {
    if (calUi_ == 1) action_ = kDialCalCancel;  // abort, keep the old calibration
    calUi_ = 0;
    return;
  }
  if (page_ == kCompass) {  // long press on the compass: into the menu pages, starting at Settings
    page_ = kMenu;
    menuOpen_ = false;
    menuCat_ = -1;
    editing_ = false;
    return;
  }
  if (page_ == kMenu && menuOpen_) leaveMenu();
  menuOpen_ = false;
  menuCat_ = -1;
  page_ = kCompass;
  editing_ = false;
}

// ---- drawing -----------------------------------------------------------------------------------------------------
void DialUi::title(Canvas& c, const char* t, int y) { c.text(kFontMedium, 120, y, Center, kGreen, t); }

void DialUi::drawDots(Canvas& c) {
  const int n = kPageCount - 1, gap = 11, cur = page_ - kWifi;
  int x0 = 120 - (n - 1) * gap / 2;
  for (int i = 0; i < n; ++i) c.fillCircle(x0 + i * gap, 228, i == cur ? 4 : 2, i == cur ? kGreen : kGrey);
}

// Instrument: rotating card with a fixed aircraft symbol and a heading bug on the rim.
void DialUi::drawInstrument(Canvas& c, float h) {
  int x0, y0, x1, y1;
  for (int d = 0; d < 360; d += 5) {
    float ang = (float)d - h;
    bool lab = d % 30 == 0, maj = d % 10 == 0;
    polar(104, ang, x0, y0);
    polar(lab ? 88 : (maj ? 94 : 98), ang, x1, y1);
    c.line(x0, y0, x1, y1, lab ? 3 : (maj ? 2 : 1), kWhite);
  }
  char b[8];
  for (int d = 0; d < 360; d += 30) {
    int lx, ly;
    polar(74, (float)d - h, lx, ly);
    const bool card = d % 90 == 0;
    ctext(c, card ? kFontMedium : kFontSmall, lx, ly, d == 0 ? kOrange : kWhite, labelFor(d, b, sizeof b));
  }
  // heading bug: a tab on the rim
  {
    float a = (float)set_.bug - h;
    int ax, ay, bx, by, cx, cy, dx, dy;
    polar(110, a - 5.0f, ax, ay);
    polar(110, a + 5.0f, bx, by);
    polar(92, a + 2.5f, cx, cy);
    polar(92, a - 2.5f, dx, dy);
    c.fillTriangle(ax, ay, bx, by, cx, cy, kCyan);
    c.fillTriangle(ax, ay, cx, cy, dx, dy, kCyan);
  }
  // fixed lubber mark and aircraft symbol
  c.fillTriangle(112, 0, 128, 0, 120, 16, kYellow);
  c.line(120, 98, 120, 146, 4, kYellow);
  c.line(90, 122, 150, 122, 4, kYellow);
  c.line(109, 142, 131, 142, 3, kYellow);
  // digital heading and bug value
  char t[16];
  snprintf(t, sizeof t, "%03d", (int)lroundf(h) % 360);
  c.roundRect(92, 68, 56, 26, kWhite, false);
  ctext(c, kFontMedium, 120, 81, kWhite, t);
  snprintf(t, sizeof t, "BUG %03u", (unsigned)set_.bug);
  ctext(c, kFontSmall, 120, 166, kCyan, t);
}

// Whiskey compass: brass bowl, rotating black card, fixed lubber line, no bug.
void DialUi::drawWhiskey(Canvas& c, float h) {
  c.fillCircle(120, 120, 120, kBrass);
  c.fillCircle(120, 120, 113, kDark);
  c.fillCircle(120, 120, 110, kBlack);
  int x0, y0, x1, y1;
  for (int d = 0; d < 360; d += 5) {
    float ang = (float)d - h;
    bool lab = d % 30 == 0, maj = d % 10 == 0;
    polar(104, ang, x0, y0);
    polar(lab ? 88 : (maj ? 94 : 98), ang, x1, y1);
    c.line(x0, y0, x1, y1, lab ? 3 : (maj ? 2 : 1), kWhite);
  }
  char b[8];
  for (int d = 0; d < 360; d += 30) {
    int lx, ly;
    polar(68, (float)d - h, lx, ly);
    const bool card = d % 90 == 0;
    ctext(c, card ? kFontMedium : kFontSmall, lx, ly, d == 0 ? kOrange : kWhite, labelFor(d, b, sizeof b));
  }
  c.ring(120, 120, 30, 1, kDark);
  c.fillCircle(120, 120, 4, kGrey);
  c.line(120, 4, 120, 30, 4, kWhite);  // lubber line
}

// Vertical card compass: the card runs sideways behind a window with a fixed index line, no bug.
void DialUi::drawVertical(Canvas& c, float h) {
  const int WX = 25, WY = 62, WW = 190, WH = 116;
  const float kPxPerDeg = 2.0f;
  c.clear(kPlate);
  c.fillRect(WX - 5, WY - 5, WW + 10, WH + 10, kFrame);
  c.fillRect(WX, WY, WW, WH, kBlack);

  char b[8];
  for (int d = 0; d < 360; d += 5) {
    float dx = fmodf((float)d - h + 540.0f, 360.0f) - 180.0f;  // -180..180, higher values to the left
    int x = 120 - (int)lroundf(dx * kPxPerDeg);
    if (x < WX - 30 || x > WX + WW + 30) continue;
    bool lab = d % 30 == 0, maj = d % 10 == 0;
    int len = lab ? 26 : (maj ? 16 : 9);
    if (x >= WX + 1 && x <= WX + WW - 2) c.fillRect(x - (lab ? 1 : 0), 130, lab ? 3 : 2, len, kWhite);
    if (lab) ctext(c, kFontMedium, x, 98, d == 0 ? kOrange : kWhite, labelFor(d, b, sizeof b));
  }
  // cover what the card shows outside the window
  c.fillRect(0, WY - 5, WX - 5, WH + 10, kPlate);
  c.fillRect(WX + WW + 5, WY - 5, 240 - (WX + WW + 5), WH + 10, kPlate);
  c.fillRect(WX - 5, WY - 5, 5, WH + 10, kFrame);
  c.fillRect(WX + WW, WY - 5, 5, WH + 10, kFrame);
  c.fillRect(119, WY, 3, WH, kWhite);  // index line

  const int sx[4] = {48, 192, 48, 192}, sy[4] = {40, 40, 196, 196};
  for (int i = 0; i < 4; ++i) {
    c.fillCircle(sx[i], sy[i], 7, kScrew);
    c.line(sx[i] - 4, sy[i] + 3, sx[i] + 4, sy[i] - 3, 2, kBlack);
  }
  c.text(kFontTiny, 120, 198, Center, kGrey, "GS-AVIONIK");
}

void DialUi::drawNoData(Canvas& c, const DialEnv& e) {
  c.roundRect(34, 100, 172, 40, kRed, false, kBlack);
  ctext(c, kFontSmall, 120, 120, kRed, e.comp.sensor == compass::kSensorNone ? "No sensor" : "No data");
}

void DialUi::drawWifi(Canvas& c, const DialEnv& e) {
  title(c, "WiFi", 14);
  if (!e.wifiOn) {
    c.text(kFontSmall, 120, 80, Center, kGrey, "Press the wheel");
    c.text(kFontSmall, 120, 102, Center, kGrey, "to switch on");
    c.text(kFontTiny, 120, 156, Center, kGrey, "WiFi off gives the best");
    c.text(kFontTiny, 120, 174, Center, kGrey, "compass accuracy");
    drawDots(c);
    return;
  }
  char b[40];
  c.text(kFontSmall, 120, 40, Center, kGrey, "SSID");
  c.text(kFontSmall, 120, 58, Center, kWhite, e.ssid);
  c.text(kFontSmall, 120, 86, Center, kGrey, "Key");
  c.text(kFontSmall, 120, 104, Center, kWhite, e.key);
  c.text(kFontSmall, 120, 132, Center, kGrey, "IP");
  c.text(kFontSmall, 120, 150, Center, kWhite, e.ip);
  if (e.ota) {
    snprintf(b, sizeof b, "Update %u kB", (unsigned)e.otaKb);
    c.text(kFontSmall, 120, 182, Center, kYellow, b);
  } else {
    snprintf(b, sizeof b, "%u client%s", (unsigned)e.clients, e.clients == 1 ? "" : "s");
    c.text(kFontSmall, 120, 182, Center, e.clients ? kGreen : kGrey, b);
    c.text(kFontTiny, 120, 206, Center, kGrey, "Press = off");
  }
  drawDots(c);
}

void DialUi::drawInfo(Canvas& c, const DialEnv& e) {
  char b[40];
  c.text(kFontMedium, 120, 36, Center, kGreen, "GS Avionik");
  c.text(kFontSmall, 120, 74, Center, kWhite, "GS-Micro-Compass");
  snprintf(b, sizeof b, "Firmware %s", e.fw);
  c.text(kFontSmall, 120, 106, Center, kGrey, b);
  c.text(kFontSmall, 120, 168, Center, kGreen, "www.gs-avionik.de");
  drawDots(c);
}

void DialUi::drawMenu(Canvas& c, const DialEnv& e) {
  static const char* const catNames[kCats] = {"Display", "Compass", "System"};
  title(c, menuOpen_ && menuCat_ >= 0 ? catNames[menuCat_] : "Settings", 12);
  if (!menuOpen_) {
    c.text(kFontSmall, 120, 96, Center, kGrey, "Press the wheel");
    c.text(kFontSmall, 120, 118, Center, kGrey, "to open");
    drawDots(c);
    return;
  }
  static const char* const labels[] = {"Brightness", "Style", "Start", "Calibrate", "Offset", "Declination", "WiFi at boot", "Restart", "Factory reset", "Back"};
  static const char* const styleNames[kStyleCount] = {"Instrument", "Whiskey", "Vertical"};
  static const char* const startNames[3] = {"Compass", "WiFi", "Info"};
  const int total = listLen();
  for (int r = 0; r < 5; ++r) {
    int i = menuTop_ + r;
    if (i >= total) break;
    int y = 42 + r * 32;
    bool sel = i == menuSel_;
    if (sel) c.roundRect(24, y - 2, 192, 28, editing_ ? kGreen : kGrey, false);
    uint16_t col = sel ? kWhite : kGrey;
    if (menuCat_ < 0) {  // category list
      c.text(kFontSmall, 38, y + 2, Left, col, i < kCats ? catNames[i] : "Back");
      if (i < kCats) c.text(kFontSmall, 202, y + 2, Right, col, ">");
      continue;
    }
    int n;
    int item = catItems(menuCat_, n)[i];
    c.text(kFontSmall, 38, y + 2, Left, col, labels[item]);
    char b[16] = "";
    const char* v = b;
    uint16_t vc = col;
    switch (item) {
      case kMiBright: snprintf(b, sizeof b, "%u%%", set_.dialBright); break;
      case kMiStyle: v = styleNames[set_.style < kStyleCount ? set_.style : 0]; break;
      case kMiStart: v = startNames[set_.startPage < 3 ? set_.startPage : 0]; break;
      case kMiOffset:
        if (set_.offsetDeg) snprintf(b, sizeof b, "%+d", (int)set_.offsetDeg);
        else snprintf(b, sizeof b, "0");
        break;
      case kMiDecl: {
        int a = abs((int)set_.declTenths);
        snprintf(b, sizeof b, "%s%d.%d", set_.declTenths < 0 ? "-" : (set_.declTenths > 0 ? "+" : ""), a / 10, a % 10);
        break;
      }
      case kMiWifiBoot: v = set_.wifiBoot ? "ON" : "OFF"; vc = set_.wifiBoot ? kGreen : kGrey; break;
      default: break;
    }
    if (*v) c.text(kFontSmall, 202, y + 2, Right, sel && editing_ ? kGreen : vc, v);
  }
  if (menuCat_ == 1) {  // live heading while Offset / Declination are adjusted
    char b[24];
    snprintf(b, sizeof b, "Heading %03d", (int)lroundf(heading(e)) % 360);
    const bool adj = editing_ && (curItem() == kMiOffset || curItem() == kMiDecl);
    c.text(kFontSmall, 120, 184, Center, adj ? kGreen : kGrey, b);
  }
  drawDots(c);
}

void DialUi::drawConfirm(Canvas& c) {
  c.text(kFontMedium, 120, 52, Center, kRed, "Factory reset?");
  c.text(kFontSmall, 120, 88, Center, kWhite, "Settings are lost");
  c.roundRect(40, 126, 160, 32, confirm_ == 1 ? kGreen : kGrey, false);
  c.text(kFontSmall, 120, 133, Center, confirm_ == 1 ? kWhite : kGrey, "Cancel");
  c.roundRect(40, 166, 160, 32, confirm_ == 2 ? kRed : kGrey, false);
  c.text(kFontSmall, 120, 173, Center, confirm_ == 2 ? kWhite : kGrey, "Reset now");
}

void DialUi::drawCal(Canvas& c, const DialEnv& e) {
  title(c, "Calibrate", 14);
  if (calUi_ == 2) {
    const bool ok = calResult_ == 1;
    c.text(kFontMedium, 120, 84, Center, ok ? kGreen : kRed, ok ? "Saved" : "Failed");
    c.text(kFontSmall, 120, 124, Center, kGrey, ok ? "Calibration stored" : "Turn the unit more");
    if (!ok) c.text(kFontSmall, 120, 146, Center, kGrey, "and try again");
    return;
  }
  c.text(kFontSmall, 120, 42, Center, kWhite, "Turn slowly in");
  c.text(kFontSmall, 120, 62, Center, kWhite, "all directions");
  char b[8];
  unsigned secs = calSeen_ ? (unsigned)((e.comp.calRemainingMs + 999) / 1000) : 20;
  snprintf(b, sizeof b, "%u", secs);
  c.text(kFontLarge, 120, 84, Center, kGreen, b);
  const int bx = 48, bw = 148;
  const int ys[2] = {152, 172};
  const uint8_t prog[2] = {e.comp.calProgX, e.comp.calProgY};
  const char* names[2] = {"X", "Y"};
  for (int i = 0; i < 2; ++i) {
    c.text(kFontTiny, 30, ys[i] - 2, Left, kGrey, names[i]);
    c.roundRect(bx, ys[i], bw, 12, kGrey, false);
    int w = bw * prog[i] / 100;
    if (w > 12) c.roundRect(bx, ys[i], w, 12, prog[i] >= 100 ? kGreen : kCyan, true);
  }
  c.text(kFontTiny, 120, 204, Center, kGrey, "Press = done");
}

void DialUi::drawSplash(Canvas& c, const DialEnv& e) {
  char b[32];
  snprintf(b, sizeof b, "v%s", e.fw);
  bootanim::draw(c, compass::State::age(now_, t0_), "GS Avionik", "GS-Micro-Compass", b);
}

void DialUi::render(Canvas& c, const DialEnv& env, uint32_t now) {
  c.clear(kBlack);
  now_ = now;
  wifiOn_ = env.wifiOn;
  otaActive_ = env.ota;
  // the heading bug is stored two seconds after the last turn
  if (bugDirty_ && compass::State::age(now, bugT_) > 2000 && !menuOpen_) {
    action_ = kDialSave;
    orig_.bug = set_.bug;
    bugDirty_ = false;
  }
  // calibration screen follows the state reported by loop()
  if (calUi_ == 1) {
    if (env.comp.calActive) calSeen_ = true;
    if (calSeen_ && !env.comp.calActive) {
      calUi_ = 2;
      calResult_ = env.comp.calResult;
      calT_ = now;
    } else if (!calSeen_ && compass::State::age(now, calT_) > 1500) {
      calUi_ = 2;  // loop() never started it (no sensor)
      calResult_ = 2;
      calT_ = now;
    }
  }
  if (calUi_ == 2 && compass::State::age(now, calT_) > 2500) calUi_ = 0;

  if (rebooting_) {
    c.text(kFontMedium, 120, 100, Center, kGreen, action_ == kDialFactoryReset ? "Resetting..." : "Restarting...");
    return;
  }
  if (compass::State::age(now, t0_) < bootanim::kDurationMs) {
    splashShown_ = true;
    drawSplash(c, env);
    return;
  }
  splashShown_ = false;
  if (confirm_) {
    drawConfirm(c);
    return;
  }
  if (calUi_) {
    drawCal(c, env);
    return;
  }
  switch (page_) {
    case kCompass: {
      const bool live = env.comp.sensor != compass::kSensorNone && env.comp.fresh(now);
      const float h = heading(env);
      lastHeading_ = h;
      if (set_.style == kStyleWhiskey) drawWhiskey(c, h);
      else if (set_.style == kStyleVertical) drawVertical(c, h);
      else drawInstrument(c, h);
      if (!live) drawNoData(c, env);
      break;
    }
    case kWifi: drawWifi(c, env); break;
    case kInfo: drawInfo(c, env); break;
    case kMenu: drawMenu(c, env); break;
  }
}
