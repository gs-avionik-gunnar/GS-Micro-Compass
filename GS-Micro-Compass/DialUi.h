// Page logic and drawing for the round display.
// Pure C++ (no Arduino dependencies) so it can be rendered and tested on a PC.
// Input: wheel rotation, short click, long press. Output: pages drawn into a gfx::Canvas
// and a few actions (save settings, reboot, calibration, WiFi) for the hardware layer.
#pragma once
#include <stdint.h>

#include "Compass.h"
#include "Gfx.h"
#include "Settings.h"

struct DialEnv {
  char ip[16];        // address of the module (access point)
  char ssid[33];
  char key[64];
  uint8_t clients;    // WiFi stations
  bool wifiOn;        // access point + web server running
  bool ota;           // firmware upload in progress
  uint32_t otaKb;
  const char* fw;
  const char* hw;
  compass::State comp;  // snapshot of the sensor / heading / calibration state
};

enum DialAction {
  kDialNone = 0,
  kDialSave,
  kDialSaveReboot,
  kDialFactoryReset,
  kDialCalStart,
  kDialCalStop,    // finish the running calibration now
  kDialCalCancel,  // abort, keep the old calibration
  kDialWifiOn,
  kDialWifiOff
};

class DialUi {
 public:
  // The compass is the home page. A long press leads to the menu pages WiFi, Settings and Info (an endless ring
  // turned with the wheel); a long press there leads back to the compass. Info is always the last page.
  enum Page { kCompass, kWifi, kMenu, kInfo, kPageCount };
  enum Style { kStyleInstrument = 0, kStyleWhiskey = 1, kStyleVertical = 2, kStyleCount = 3 };

  void begin(const Settings& s, uint32_t now);  // the first page is s.startPage
  void onRotate(int steps);  // + clockwise
  void onClick();
  void onLongPress();
  // Draws the whole screen into the current band of c.
  void render(gfx::Canvas& c, const DialEnv& env, uint32_t now);

  // Results for the hardware layer
  DialAction takeAction() { DialAction a = action_; action_ = kDialNone; return a; }
  const Settings& settings() const { return set_; }
  bool takeBeep() { bool b = beep_; beep_ = false; return b; }
  bool takeDirty() { bool d = dirty_; dirty_ = false; return d; }
  bool fastRedraw() const;  // true while the screen shows live compass data
  int page() const { return page_; }
  void gotoPage(int p) { page_ = p; dirty_ = true; }

  // Test hooks
  int menuIndex() const { return menuSel_; }
  int menuCategory() const { return menuCat_; }  // -1 = top level
  bool editing() const { return editing_; }
  bool menuOpen() const { return menuOpen_; }
  int confirmState() const { return confirm_; }
  int calState() const { return calUi_; }

 private:
  enum MenuItem { kMiBright, kMiStyle, kMiStart, kMiCalibrate, kMiOffset, kMiDecl, kMiWifiBoot, kMiRestart, kMiReset, kMiBack };
  // Settings are grouped: top level = categories (Display, Compass, System, Back), menuCat_ = open category or -1.
  enum { kCats = 3 };
  static const uint8_t* catItems(int cat, int& len);
  int listLen() const;
  int curItem() const;  // item under the cursor inside an open category
  float heading(const DialEnv& e) const;

  void drawInstrument(gfx::Canvas& c, float h);
  void drawWhiskey(gfx::Canvas& c, float h);
  void drawVertical(gfx::Canvas& c, float h);
  void drawNoData(gfx::Canvas& c, const DialEnv& e);
  void drawWifi(gfx::Canvas& c, const DialEnv& e);
  void drawInfo(gfx::Canvas& c, const DialEnv& e);
  void drawMenu(gfx::Canvas& c, const DialEnv& e);
  void drawConfirm(gfx::Canvas& c);
  void drawCal(gfx::Canvas& c, const DialEnv& e);
  void drawSplash(gfx::Canvas& c, const DialEnv& e);
  void drawDots(gfx::Canvas& c);
  void title(gfx::Canvas& c, const char* t, int y = 12);
  void leaveMenu();
  bool splashActive() const;  // the boot animation is on screen
  void skipSplash();

  Settings set_;      // working copy of the settings
  Settings orig_;     // as loaded / saved, to detect changes
  uint32_t t0_ = 0, now_ = 0;
  int page_ = kCompass;
  int menuSel_ = 0, menuTop_ = 0, menuCat_ = -1;
  bool editing_ = false;
  bool menuOpen_ = false;  // the Settings page is only shown until the wheel is pressed
  int confirm_ = 0;        // 0 off, 1 = Cancel selected, 2 = Reset selected
  int calUi_ = 0;          // 0 off, 1 calibration screen, 2 result shown
  bool calSeen_ = false;   // loop() has started the calibration
  uint32_t calT_ = 0;      // start (state 1) or result time (state 2)
  uint8_t calResult_ = 0;
  float lastHeading_ = 0.0f;  // heading shown last, used by the click that sets the bug
  bool bugDirty_ = false;
  uint32_t bugT_ = 0;
  bool wifiOn_ = false, otaActive_ = false;  // from the last environment
  DialAction action_ = kDialNone;
  bool beep_ = false;
  bool dirty_ = true;
  bool rebooting_ = false;
  bool splashShown_ = false;  // the boot animation has been drawn (input then skips it instead of acting)
};
