#pragma once

// One place for the firmware version: boot screen, info page, WebUI, README.
#define GS_FW_VERSION "2.0"
#define GS_PRODUCT_NAME "GS-Micro-Compass"

// ---- Magnetometer GY-271 (QMC5883L or HMC5883L, detected automatically) on the Grove port ------------------
#define PIN_I2C_SDA 13
#define PIN_I2C_SCL 15
#define I2C_FREQ 400000

// ---- Network (access point, only while WiFi is switched on) -----------------------------------------------------
#define AP_IP_A 192
#define AP_IP_B 168
#define AP_IP_C 1
#define AP_IP_D 1
#define AP_CHANNEL 1
#define AP_MAX_STATIONS 4

// ---- Hardware (ESP32-S3, GC9A01 240x240 round display) ------------------------
// Pins as on the GS-Micro-Compass hardware; if your unit differs (or colours/orientation look wrong) change them here.
// The LED ring of other GS products is NOT used here: GPIO 13 belongs to the sensor (I2C SDA).
#define PIN_DIAL_HOLD 46       // keeps the power on (battery operation)
#define PIN_DIAL_LCD_MOSI 5
#define PIN_DIAL_LCD_SCLK 6
#define PIN_DIAL_LCD_CS 7
#define PIN_DIAL_LCD_DC 4
#define PIN_DIAL_LCD_RST 8
#define PIN_DIAL_LCD_BL 9
#define PIN_DIAL_ENC_A 40
#define PIN_DIAL_ENC_B 41
#define PIN_DIAL_BUTTON 42
#define PIN_DIAL_BUZZER 3
#define DIAL_LCD_MADCTL 0x48   // 0x08 = BGR; +0x40 mirrors horizontally (needed on this unit), +0x80 vertically
#define DIAL_LCD_INVERT 1      // GC9A01 panels on the Dial need inversion on
#define DIAL_ENC_TRANSITIONS_PER_DETENT 4  // set to 2 if one click of the wheel jumps two steps
#define DIAL_ENC_INVERT 0      // 1 = reverse the wheel direction
#define DIAL_BEEP 1            // short click on every wheel step / press
