#include "state.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_timer.h>

AppState g_state;
static SemaphoreHandle_t s_mutex = nullptr;

void stateInit() {
  if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
  for (int i = 0; i < HISTORY_LEN; i++) {
    g_state.histSpeed[i] = 0;
    g_state.histRpm[i] = 0;
    g_state.histTemp[i] = 0;
  }
}

void stateLock()   { xSemaphoreTake(s_mutex, portMAX_DELAY); }
void stateUnlock() { xSemaphoreGive(s_mutex); }

const char* gearStr(uint8_t gear) {
  static const char* const names[] = {"P", "R", "N", "D", "1", "2", "3", "4", "5", "6"};
  return gear < 10 ? names[gear] : "-";
}

const char* busStr(BusState b) {
  switch (b) {
    case BusState::OK:     return "OK";
    case BusState::BUSOFF: return "BUSOFF";
    default:               return "IDLE";
  }
}

uint32_t uptimeSeconds() { return (uint32_t)(esp_timer_get_time() / 1000000ULL); }

void stateSetPaused(bool paused) {
  StateGuard g;
  g_state.paused = paused;
  g_state.controlRev++;   // siempre: tras cualquier comando se difunden frames + state
}

void stateClearFrames() {
  StateGuard g;
  for (auto& f : g_state.frames) f = FrameEntry();
  g_state.framesRev++;
  g_state.controlRev++;
}

void stateCopyHistory(float* speed, int16_t* rpm, int16_t* temp) {
  StateGuard g;
  for (int i = 0; i < HISTORY_LEN; i++) {
    int idx = (g_state.histHead + i) % HISTORY_LEN;
    if (speed) speed[i] = g_state.histSpeed[idx];
    if (rpm)   rpm[i]   = g_state.histRpm[idx];
    if (temp)  temp[i]  = g_state.histTemp[idx];
  }
}

void statePushSample() {
  StateGuard g;
  uint16_t h = g_state.histHead;
  g_state.histSpeed[h] = g_state.v.speed;
  g_state.histRpm[h]   = (int16_t)g_state.v.rpm;
  g_state.histTemp[h]  = (int16_t)g_state.v.temp;
  g_state.histHead = (h + 1) % HISTORY_LEN;
  g_state.sampleRev++;
}

int stateSnapshotFrames(FrameEntry* out, uint32_t* rev) {
  int n = 0;
  {
    StateGuard g;
    for (const auto& f : g_state.frames)
      if (f.used) out[n++] = f;
    if (rev) *rev = g_state.framesRev;
  }
  // inserción por ID (n <= 12)
  for (int i = 1; i < n; i++) {
    FrameEntry k = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].id > k.id) { out[j + 1] = out[j]; j--; }
    out[j + 1] = k;
  }
  return n;
}
