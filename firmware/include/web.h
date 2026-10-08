// Servidor HTTP (LittleFS + API REST) y WebSocket /ws según el protocolo de docs/SPEC.md.
#pragma once
#include <Arduino.h>

void webBegin();   // monta LittleFS, registra rutas, arranca el servidor y la tarea de difusión
