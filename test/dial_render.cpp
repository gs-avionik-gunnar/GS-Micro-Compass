// Renders every dial page to PPM files (host tool, used to check the layouts by eye).
#include <stdio.h>
#include <string.h>

#include "DialUi.h"
#include "Settings.h"

void settingsDefaultCredentials(char* ssid, size_t ssidCap, char* key, size_t keyCap) {
  snprintf(ssid, ssidCap, "GS-MC-E5F6");
  snprintf(key, keyCap, "A1B2C3D4E5F6");
}

static uint16_t buf[240 * 30];
static DialEnv env;

static void dump(DialUi& ui, uint32_t now, const char* name) {
  FILE* f = fopen(name, "wb");
  fprintf(f, "P6\n240 240\n255\n");
  gfx::Canvas c;
  for (int y0 = 0; y0 < 240; y0 += 30) {
    c.setBand(buf, y0, 30);
    ui.render(c, env, now);
    for (int i = 0; i < 240 * 30; ++i) {
      uint16_t v = (uint16_t)((buf[i] << 8) | (buf[i] >> 8));
      int x = i % 240, y = y0 + i / 240;
      bool inside = (x - 119.5) * (x - 119.5) + (y - 119.5) * (y - 119.5) <= 120.0 * 120.0;
      uint8_t px[3] = {(uint8_t)(((v >> 11) & 31) << 3), (uint8_t)(((v >> 5) & 63) << 2), (uint8_t)((v & 31) << 3)};
      if (!inside) { px[0] = px[1] = px[2] = 60; if ((x + y) & 1) px[0] = px[1] = px[2] = 90; }
      fwrite(px, 1, 3, f);
    }
  }
  fclose(f);
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  char name[256];
  Settings s;
  settingsDefaults(s);
  s.dialBright = 80;
  s.bug = 95;
  uint32_t now = 10000;
  env = DialEnv();
  strcpy(env.ip, "192.168.1.1");
  strcpy(env.ssid, "GS-MC-E5F6");
  strcpy(env.key, "A1B2C3D4E5F6");
  env.fw = "2.0";
  env.hw = "GS-Micro-Compass";
  env.comp.sensor = compass::kSensorQmc;
  env.comp.lastRxMs = now;
  env.comp.calibrated = true;
  env.comp.rawDeg = 300.0f;  // + 180 offset + 4.0 decl = 124

  // the three compass styles at two headings
  const float raws[2] = {300.0f, 21.0f};
  const char* sn[3] = {"instrument", "whiskey", "vertical"};
  for (int st = 0; st < 3; ++st) {
    for (int k = 0; k < 2; ++k) {
      s.style = (uint8_t)st;
      env.comp.rawDeg = raws[k];
      DialUi ui;
      ui.begin(s, 0);
      snprintf(name, sizeof name, "%s/compass_%s_%d.ppm", dir, sn[st], k);
      dump(ui, now, name);
    }
  }
  // no sensor overlay
  { s.style = 0; DialUi ui; ui.begin(s, 0); env.comp.sensor = compass::kSensorNone;
    snprintf(name, sizeof name, "%s/nosensor.ppm", dir); dump(ui, now, name); env.comp.sensor = compass::kSensorQmc; }
  env.comp.rawDeg = 300.0f;
  s.style = 0;
  DialUi ui;
  ui.begin(s, 0);
  // wifi off / on / ota
  ui.gotoPage(DialUi::kWifi);
  snprintf(name, sizeof name, "%s/wifi_off.ppm", dir); dump(ui, now, name);
  env.wifiOn = true; env.clients = 1;
  snprintf(name, sizeof name, "%s/wifi_on.ppm", dir); dump(ui, now, name);
  env.ota = true; env.otaKb = 412;
  snprintf(name, sizeof name, "%s/wifi_ota.ppm", dir); dump(ui, now, name);
  env.ota = false; env.wifiOn = false;
  ui.gotoPage(DialUi::kInfo);
  snprintf(name, sizeof name, "%s/info.ppm", dir); dump(ui, now, name);
  ui.gotoPage(DialUi::kMenu);
  snprintf(name, sizeof name, "%s/menu_closed.ppm", dir); dump(ui, now, name);
  // settings menus
  { DialUi mu; mu.begin(s, 0); mu.gotoPage(DialUi::kMenu); mu.onClick();
    snprintf(name, sizeof name, "%s/set_top.ppm", dir); dump(mu, now, name);
    mu.onClick();  // Display
    snprintf(name, sizeof name, "%s/set_display.ppm", dir); dump(mu, now, name);
    mu.onRotate(1); mu.onClick();  // edit style
    snprintf(name, sizeof name, "%s/set_display_edit.ppm", dir); dump(mu, now, name);
    mu.onClick();                                   // end editing
    mu.onRotate(1); mu.onRotate(1); mu.onClick();  // Back (4th row) -> category list, cursor on Display
    mu.onRotate(1); mu.onClick();                   // Compass
    snprintf(name, sizeof name, "%s/set_compass.ppm", dir); dump(mu, now, name);
    mu.onRotate(1); mu.onClick();  // edit offset
    snprintf(name, sizeof name, "%s/set_compass_edit.ppm", dir); dump(mu, now, name); }
  { DialUi mu; mu.begin(s, 0); mu.gotoPage(DialUi::kMenu); mu.onClick();
    mu.onRotate(1); mu.onRotate(1); mu.onClick();  // System
    snprintf(name, sizeof name, "%s/set_system.ppm", dir); dump(mu, now, name);
    mu.onRotate(1); mu.onRotate(1); mu.onClick();  // Factory reset confirm
    snprintf(name, sizeof name, "%s/confirm.ppm", dir); dump(mu, now, name); }
  // calibration
  { DialUi cu; cu.begin(s, 0); cu.gotoPage(DialUi::kMenu); cu.onClick(); cu.onRotate(1); cu.onClick(); cu.onClick();  // Compass > Calibrate
    env.comp.calActive = true; env.comp.calRemainingMs = 13200; env.comp.calProgX = 70; env.comp.calProgY = 35;
    snprintf(name, sizeof name, "%s/cal_run.ppm", dir); dump(cu, now, name);
    env.comp.calActive = false; env.comp.calResult = 1;
    snprintf(name, sizeof name, "%s/cal_ok.ppm", dir); dump(cu, now + 50, name);
    env.comp.calResult = 0; }
  // boot animation
  { DialUi su; su.begin(s, 0);
    const uint32_t ts[8] = {250, 600, 950, 1250, 1550, 1850, 2150, 2450};
    for (int i = 0; i < 8; ++i) { snprintf(name, sizeof name, "%s/boot_%d.ppm", dir, i); dump(su, ts[i], name); } }
  { DialUi su; su.begin(s, now - 100); snprintf(name, sizeof name, "%s/splash.ppm", dir); dump(su, now + 2300, name); }
  return 0;
}
