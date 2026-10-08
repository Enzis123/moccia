// Estado compartido de la aplicación, protegido por un mutex FreeRTOS.
#pragma once
#include <Arduino.h>
#include "config.h"

enum class BusState : uint8_t { OK = 0, BUSOFF, IDLE };

struct VehicleData {
  int      rpm    = 0;
  float    speed  = 0.0f;   // km/h (siempre en km/h internamente)
  int      temp   = 0;      // °C
  int      fuel   = 0;      // %
  float    batt   = 0.0f;   // V
  uint8_t  gear   = 0;      // 0=P 1=R 2=N 3=D 4..9 = 1..6
  uint8_t  lights = 0;      // bitmask (ver SPEC)
};

struct FrameEntry {
  bool     used   = false;
  bool     ext    = false;
  uint32_t id     = 0;
  uint8_t  dlc    = 0;
  uint8_t  data[8] = {0};
  uint32_t count  = 0;
  uint32_t period = 0;      // ms entre las dos últimas recepciones
  uint32_t lastMs = 0;
};

struct AppSettings {
  uint32_t bitrate   = 500000;
  bool     demo      = true;
  bool     backlight = true;
  bool     mph       = false;   // false = "kmh", true = "mph"
};

struct AppState {
  VehicleData v;
  FrameEntry  frames[FRAME_TABLE_SIZE];
  uint32_t    framesRev  = 0;   // cambia en cada modificación de la tabla
  int         fps        = 0;   // tramas/s (reales + simuladas)
  BusState    bus        = BusState::IDLE;
  bool        paused     = false;
  // Historial 1 muestra/s, buffer circular. histHead = índice de la próxima escritura.
  float       histSpeed[HISTORY_LEN];
  int16_t     histRpm[HISTORY_LEN];
  int16_t     histTemp[HISTORY_LEN];
  uint16_t    histHead   = 0;
  uint32_t    sampleRev  = 0;   // se incrementa con cada muestra nueva
  AppSettings settings;
  uint32_t    settingsRev = 0;  // se incrementa en cada cambio de ajustes
  uint32_t    controlRev  = 0;  // pausa/reanudar/limpiar => difundir estado ya
};

extern AppState g_state;

void stateInit();
void stateLock();
void stateUnlock();

struct StateGuard {
  StateGuard()  { stateLock(); }
  ~StateGuard() { stateUnlock(); }
  StateGuard(const StateGuard&) = delete;
  StateGuard& operator=(const StateGuard&) = delete;
};

// Utilidades
const char* gearStr(uint8_t gear);
const char* busStr(BusState b);
uint32_t    uptimeSeconds();

// Comandos del monitor (pantalla y web llaman a estas funciones)
void stateSetPaused(bool paused);
void stateClearFrames();

// Copia ordenada (más antigua -> más reciente) del historial. Requiere arrays de HISTORY_LEN.
void stateCopyHistory(float* speed, int16_t* rpm, int16_t* temp);
// Copia de la tabla de tramas ordenada por ID (solo entradas usadas). Devuelve cuántas.
int stateSnapshotFrames(FrameEntry* out /*[FRAME_TABLE_SIZE]*/, uint32_t* rev = nullptr);

// Añade una muestra (se llama 1 vez/s desde la tarea CAN)
void statePushSample();
