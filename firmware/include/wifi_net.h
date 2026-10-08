// WiFi: STA con credenciales de secrets.h; si falla en 10 s => AP "MocciaCAN"/"moccia1234".
// mDNS: moccia.local
#pragma once
#include <Arduino.h>

void netBegin();       // bloquea como máximo ~10 s (intento STA)
void netLoop();        // reconexión STA en segundo plano

struct NetInfo {
  char ip[16];
  char ssid[33];
  int  rssi;
  bool ap;            // true = modo AP
  bool connected;     // STA conectado o AP activo
};
NetInfo netInfo();
