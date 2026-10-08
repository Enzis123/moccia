// Ajustes persistentes (NVS / Preferences). Todos los cambios (pantalla o web) pasan por aquí,
// actualizan g_state.settings, incrementan settingsRev (=> difusión WebSocket) y se guardan.
#pragma once
#include <Arduino.h>
#include "state.h"

void settingsLoad();                      // carga de NVS a g_state.settings
bool settingsValidBitrate(uint32_t br);

bool settingsSetBitrate(uint32_t br);     // devuelve false si el valor no es válido
void settingsSetDemo(bool on);
void settingsSetBacklight(bool on);
void settingsSetMph(bool mph);

AppSettings settingsGet();                // copia protegida
