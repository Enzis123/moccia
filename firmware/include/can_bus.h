// Bus CAN (TWAI): driver, decodificación según SPEC, tabla de tramas y estado del bus.
#pragma once
#include <Arduino.h>

struct CanFrame {
  uint32_t id;
  bool     ext;
  uint8_t  dlc;
  uint8_t  data[8];
};

// Arranca el driver TWAI y la tarea de recepción (que también ejecuta el simulador).
void canBegin(uint32_t bitrate);

// Pide reinstalar el driver con otro bitrate (se aplica en la tarea CAN).
void canRequestBitrate(uint32_t bitrate);

// Procesa una trama (real o simulada): decodifica, actualiza tabla, cuenta tramas/s.
void canProcessFrame(const CanFrame& f, uint32_t nowMs);

// true si el driver TWAI está instalado y arrancado
bool canDriverOk();
