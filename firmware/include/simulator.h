// Modo demo: genera una conducción realista y la emite como tramas CAN (mapeo SPEC)
// que pasan por el mismo decodificador que las tramas reales.
#pragma once
#include <Arduino.h>

void simulatorBegin();
// Llamar con frecuencia (≈5 ms) desde la tarea CAN. valuesOnly: solo alimenta los valores
// del tablero (no cuenta como tráfico ni entra en la tabla de tramas).
void simulatorTick(uint32_t nowMs, bool valuesOnly);
