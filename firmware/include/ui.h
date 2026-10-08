// Interfaz táctil (LovyanGFX): barra superior con 4 pestañas y páginas Tablero, CAN,
// Gráficas y Ajustes. Todo el dibujo se hace en sprites (PSRAM) que se vuelcan al
// framebuffer RGB solo cuando su contenido cambia => sin parpadeo.
#pragma once
#include <Arduino.h>

void uiBegin();   // inicializa pantalla y táctil (llamar después de ch422gBegin())
void uiLoop();    // llamar continuamente desde la tarea de UI (loop())
