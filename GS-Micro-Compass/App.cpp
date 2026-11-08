#include "App.h"

Settings g_settings;
compass::State g_state;
compass::Cal g_cal = compass::defaultCal();
SemaphoreHandle_t g_stateMutex = nullptr;
const char* g_hwName = "GS-Micro-Compass";
volatile uint8_t g_calCmd = kCalNone;
volatile bool g_wifiOn = false;
volatile bool g_otaActive = false;
volatile uint32_t g_otaBytes = 0;

void appInit() {
  if (!g_stateMutex) g_stateMutex = xSemaphoreCreateMutex();
}

bool stateSnapshot(compass::State& out, uint32_t timeoutMs) {
  if (!g_stateMutex || xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) return false;
  out = g_state;
  xSemaphoreGive(g_stateMutex);
  return true;
}

void statePublish(const compass::State& s) {
  if (!g_stateMutex || xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
  g_state = s;
  xSemaphoreGive(g_stateMutex);
}
