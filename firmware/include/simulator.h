// Modo demo: genera una conducción realista y la emite como tramas CAN (mapeo SPEC)
// que pasan por el mismo decodificador que las tramas reales.
#pragma once
#include <Arduino.h>

void simulatorBegin();
void simulatorTick(uint32_t nowMs);   // llamar con frecuencia (≈5 ms) desde la tarea CAN
