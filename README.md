# GS-Micro-Compass 2.0

Magnetic compass (ESP32-S3, round 240x240 display, wheel with button, buzzer) with a GY-271 magnetometer
(QMC5883L or HMC5883L, detected automatically) on the Grove port. Three display styles, a heading bug, a settings menu and a
WebUI with firmware update that follow the GS Dial template (same boot screen, info page, menu layout and WebUI as GS-Traffic2WIFI).
Arduino IDE, **no external libraries**: display driver, fonts, wheel, buzzer and the sensor driver are part of the project.

## Hardware

- ESP32-S3, 240x240 round display, wheel with button, buzzer
- GY-271 magnetometer (QMC5883L at 0x0D or HMC5883L at 0x1E) on the Grove port: SDA = GPIO 13, SCL = GPIO 15
- No LED ring (GPIO 13 belongs to the sensor)
- Flashing: WebUI (firmware update) or USB-C; factory reset on the screen / in the menu

## Features

- Heading from the two horizontal sensor axes, low-pass filtered on the field vector (no jump at 359 -> 0)
- Three display styles: Instrument (rotating card, aircraft symbol, heading bug), Whiskey compass (brass bowl, no bug),
  Vertical card (card runs sideways behind a window, no bug)
- Heading bug (instrument style): the wheel sets it, 1 degree per click, a fast spin moves in steps of 15 degrees; a click sets it to the
  current heading; stored 2 s after the last change
- Calibration on the unit (20 s, progress bars, finish early with a click, cancel with a long press)
- Magnetic declination (-30.0 to +30.0, 0.1 steps) and offset (-180 to +180) on the unit and in the WebUI
- Sensor mounting (swap X/Y, invert X, invert Y) and smoothing in the WebUI
- WiFi access point `GS-MC-XXXX` (key = MAC address), switched on and off on the display, optional at power-up
- WebUI (English / German) with help texts and firmware update
- Settings on the display: brightness, style, start page, calibration, offset, declination, WiFi at boot, restart, factory reset
- Serial output for debugging and for the serial plotter

## What 2.0 brings

- **GS Dial template:** boot screen, info page (always last), endless scrolling through the menu pages, two-level settings menu with Back rows,
  restart, factory reset with confirmation, WebUI with the shared layout, colours, language switch and help texts.
- **Own display driver** instead of an external library (band renderer, about 14 KB RAM, no flicker).
- **WiFi is off by default.** The radio can disturb the magnetometer. Switch it on on the WiFi page (click on the wheel) or set
  "WiFi on at power-up". The access point is `GS-MC-` plus the last four hex digits of the MAC address, the key is the MAC address
  (12 hex digits). Both can be changed in the WebUI. Address: `http://192.168.1.1`.
- **Settings in the WebUI:** everything except the calibration (done on the unit).
- **Calibration is kept by a factory reset** (own NVS namespace): it describes the device, not the user's choices.
- **Takeover from 1.x:** on the first start the calibration, offset, declination, style and heading bug of firmware 1.x are read
  from the old NVS namespace. Offsets written as 0..359 are converted to -180..180 (270 becomes -90).

## Operation

The compass is the home page. It shows no menu items and no page dots, so the instrument stays clean. A **long press (0.8 s)** on the
compass leads into the menu pages; a long press on any menu page leads back to the compass.

| Page | Wheel | Click | Long press (0.8 s) |
|---|---|---|---|
| Compass, instrument style | sets the heading bug (1 degree per click, a fast spin moves in steps of 15) | heading bug = current heading | into the menu pages (Settings) |
| Compass, whiskey / vertical style | - | - | into the menu pages (Settings) |
| WiFi | next / previous menu page | switch WiFi on / off | back to the compass |
| Settings | next / previous menu page | open the menu | back to the compass |
| Info | next / previous menu page | - | back to the compass |

The menu pages WiFi, Settings and Info form an endless ring (three page dots at the bottom); the compass is not part of it.
After a long press from the compass you land on Settings, so WiFi and Info are one wheel click away.

Settings menu (two levels, endless scrolling, 5 visible rows, Back rows; click = open / edit / confirm):

| Category | Items |
|---|---|
| Display | Brightness (10-100 %), Style (Instrument / Whiskey / Vertical), Start (Compass / WiFi / Info) |
| Compass | Calibrate, Offset (-180..180), Declination (-30.0..30.0); the live heading is shown below the list |
| System | WiFi at boot (ON/OFF), Restart, Factory reset (confirmation, Cancel preselected) |

Changes are saved when the menu is left (Back on the top level or long press). Nothing in the menu needs a restart.

### Calibration
Settings > Compass > Calibrate. Hold the unit level and turn it slowly through several full circles for 20 s. The two bars show how
much of the expected range has been covered on each axis. A click finishes early, a long press cancels (the old calibration stays).
If one axis moved too little the display says "Failed" and nothing is changed.

### Sensor mounting
The defaults match the mounting used so far (sensor Y towards the back, X along the display): swap X/Y on, invert X on, invert Y off,
offset 180. If the heading decreases when you turn the unit clockwise, toggle "Invert Y". A fixed error: use the offset.

### Firmware update
1. Build in the Arduino IDE (board "ESP32S3 Dev Module", partition scheme with OTA) and export the binary (*Sketch > Export Compiled Binary*).
   Upload `GS-Micro-Compass.ino.bin` - not the `.bootloader`, `.partitions` or `.merged` files.
2. On the unit: page WiFi, click the wheel. The display shows SSID, key and address.
3. Connect, open `http://192.168.1.1`, tab Firmware, select the file, "Start update". The unit restarts by itself.
Switching WiFi off is blocked while an upload is running.

## WebUI
Tabs Status / Configuration / Firmware. Status: heading, bug, style, offset, declination, sensor type, calibration state and range,
raw values (for checking the mounting). Configuration groups (all collapsed): Display, Compass, Sensor mounting, WiFi, Debug and test.
Saving restarts the unit. API: see `TEMPLATE_SPEC.md` of the template kit (`/api/status`, `/api/config`, `/api/reset`, `/update`).
`webui/index.html` is the source of the page; `tools/make_webpage.py` generates `WebPage.h` from it.

## Build
Arduino IDE, Arduino-ESP32 core 2.0.x or 3.x, board "ESP32S3 Dev Module". No extra libraries. Open `GS-Micro-Compass/GS-Micro-Compass.ino`.

## Layout
```
GS-Micro-Compass/        the sketch
  GS-Micro-Compass.ino   setup/loop: sensor, heading, calibration, snapshot
  Config.h               version (GS_FW_VERSION) and pins
  Compass.*              heading, filter, calibration, shared state (pure C++)
  Mag.*                  GY-271 driver (I2C)
  Settings.h, SettingsCore.cpp (defaults, limits), Settings.cpp (NVS, migration)
  App.*                  shared state, mutex
  Gfx.*, DialFont.*      band renderer and fonts (copied from the template)
  DialUi.*               pages, menu, the three styles (pure C++)
  DialHw.*               display, wheel, buzzer, actions (own FreeRTOS task)
  WebUi.*, WebPage.h     access point, HTTP, OTA
webui/                   index.html (source), mock server, browser tests
test/                    PC tests, page renderer, stubs for the syntax check
tools/                   genfont.py (fonts), make_webpage.py
```
Tests: `make -C test run` (unit tests), `make -C test syntax` (glue against stubs), `make -C test dial_render && ./dial_render dir`
(writes every page as PPM), `python3 webui/test_shell.py webui/index.html` and `python3 webui/test_compass.py webui/index.html` (browser).

## Settings
NVS namespace `gsmc2` (keys up to 15 characters), calibration in `gsmcal`. Limits are defined once in `settingsSanitize()`.

## Version history
- **2.0** Port to the GS Dial template (boot screen, info page, menu, WebUI, settings, tests). Own display driver instead of an
  external library. WiFi on demand with the WebUI and firmware update. Takeover of the 1.x settings.
- **1.5.1** The MAC address for the WiFi name and key is read from the eFuse (it was all zeros before). Headings in green.
- **1.5** Firmware update through a WiFi access point started from the menu.
- **1.4** Third style: vertical card compass.
- **1.3** Selectable style: instrument or whiskey compass.
- **1.2** Declination adjustable in the menu. Boot screen with product name and version.
- **1.1** Menu on a long press: calibration, offset, info; offset stored.
- **1.0** Vertical compass in the style of an aircraft instrument with heading bug, GY-271 on the Grove port, hard-iron calibration.

## License
See `LICENSE`.
