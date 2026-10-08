#include "simulator.h"
#include <esp_random.h>
#include "can_bus.h"

namespace {

// ---- modelo de vehículo ----
struct Sim {
  float speed = 0;        // km/h
  float target = 0;       // km/h objetivo
  float rpm = 800;
  float temp = 35;        // °C
  float fuel = 72;        // %
  float batt = 12.4f;     // V
  uint8_t gear = 0;       // código SPEC
  uint8_t lights = 0x01;  // cortas encendidas
  uint32_t phaseEnd = 0;  // fin del tramo actual (ms)
  uint32_t stoppedSince = 0;
  uint32_t blinkUntil = 0;
  uint8_t  blinkSide = 0; // 0 = ninguno, 1 = izq, 2 = dcha
  uint32_t lastPhys = 0;
  uint32_t odometer = 123456;  // km*10 (para la trama 0x3E8)
  float    odoAcc = 0;
  float    steer = 0;
  uint8_t  obdPid = 0;
  uint8_t  rolling = 0;
} s;

struct Sched { uint32_t id; uint16_t periodMs; uint32_t next; };
Sched sched[] = {
    {0x100, 20, 0},  {0x101, 20, 3},   {0x102, 200, 7},  {0x103, 100, 11},
    {0x1A0, 20, 13}, {0x2C4, 50, 17},  {0x3E8, 1000, 19}, {0x4F0, 500, 23},
    {0x7E8, 250, 29}, {0x0F0, 100, 31},
};

float frand(float a, float b) { return a + (b - a) * (esp_random() / 4294967295.0f); }
uint32_t urand(uint32_t a, uint32_t b) { return a + esp_random() % (b - a + 1); }

// relación rpm por km/h de cada marcha (1..6)
const float kRatio[6] = {118.f, 68.f, 46.f, 35.f, 28.f, 23.f};

void newPhase(uint32_t now) {
  // Perfil: parado, ciudad, avenida, carretera, autopista
  static const float targets[] = {0, 30, 50, 50, 70, 90, 110, 125};
  float t = targets[urand(0, sizeof(targets) / sizeof(targets[0]) - 1)];
  if (s.target == 0 && t == 0) t = 50;  // no quedarse parado dos tramos seguidos
  s.target = t + (t > 0 ? frand(-5, 5) : 0);
  s.phaseEnd = now + (t == 0 ? urand(6000, 12000) : urand(10000, 25000));
  if (t > 0 && urand(0, 2) == 0) {  // intermitente al cambiar de tramo
    s.blinkSide = urand(1, 2);
    s.blinkUntil = now + 4000;
  }
}

void physics(uint32_t now, float dt) {
  if (now >= s.phaseEnd) newPhase(now);

  // aceleración / frenada suaves
  float diff = s.target - s.speed;
  float acc = diff > 0 ? fminf(diff, 2.5f + diff * 0.08f) : fmaxf(diff, -6.0f);
  s.speed += acc * dt;
  if (s.speed < 0.3f && s.target == 0) s.speed = 0;
  if (s.speed < 0) s.speed = 0;
  // pequeñas variaciones de tráfico
  if (s.speed > 5) s.speed += frand(-0.15f, 0.15f);

  // marcha
  if (s.speed < 0.5f) {
    if (!s.stoppedSince) s.stoppedSince = now;
    s.gear = (now - s.stoppedSince > 4000) ? 0 /*P*/ : 2 /*N*/;
  } else {
    s.stoppedSince = 0;
    int g;
    if (s.speed < 15) g = 1;
    else if (s.speed < 32) g = 2;
    else if (s.speed < 52) g = 3;
    else if (s.speed < 75) g = 4;
    else if (s.speed < 100) g = 5;
    else g = 6;
    s.gear = 3 + g;  // 4..9 => 1..6
  }

  // rpm
  float targetRpm;
  if (s.gear <= 3) targetRpm = 800 + frand(-20, 20);
  else {
    targetRpm = s.speed * kRatio[s.gear - 4] + (diff > 2 ? 400 : 0);
    if (targetRpm < 900) targetRpm = 900;
  }
  if (targetRpm > 6800) targetRpm = 6800;
  s.rpm += (targetRpm - s.rpm) * fminf(1.0f, dt * 6.0f);

  // temperatura: calentamiento hasta ~90 °C y ciclo del termostato
  float tgtTemp = 88 + 4 * sinf(now / 40000.0f) + (s.rpm > 4000 ? 3 : 0);
  s.temp += (tgtTemp - s.temp) * dt * 0.02f;

  // combustible
  s.fuel -= (0.0004f + s.rpm * 0.0000008f) * dt * 10;
  if (s.fuel < 4) s.fuel = 80;

  // batería: alternador
  float tgtBatt = 13.9f + 0.3f * (s.rpm > 1500) + frand(-0.05f, 0.05f);
  s.batt += (tgtBatt - s.batt) * dt * 0.5f;

  // odómetro
  s.odoAcc += s.speed * dt / 360.0f;  // décimas de km
  while (s.odoAcc >= 1) { s.odometer++; s.odoAcc -= 1; }

  // volante
  s.steer += (frand(-1, 1) * (s.speed > 2 ? 4 : 0) - s.steer * 0.5f) * dt;

  // luces
  uint8_t l = 0x01;                               // cortas
  if (s.speed > 95) l |= 0x02;                    // largas en autopista
  if (now < s.blinkUntil && ((now / 333) & 1)) l |= (s.blinkSide == 1 ? 0x04 : 0x08);
  if (s.gear == 0) l |= 0x10;                     // freno de mano en P
  if (s.temp > 99) l |= 0x20;                     // check engine si se calienta
  s.lights = l;
}

void put16(uint8_t* d, uint16_t v) { d[0] = v >> 8; d[1] = v & 0xFF; }

void emit(uint32_t id, uint32_t now, bool valuesOnly) {
  CanFrame f;
  f.id = id;
  f.ext = false;
  f.dlc = 8;
  memset(f.data, 0, 8);
  uint8_t* d = f.data;
  switch (id) {
    case 0x100:
      put16(d, (uint16_t)s.rpm);
      d[2] = s.gear;
      d[3] = (uint8_t)(s.rpm > 900 ? 30 + (s.rpm / 120) : 12);  // carga motor %
      d[7] = s.rolling;
      break;
    case 0x101:
      put16(d, (uint16_t)(s.speed * 100.0f + 0.5f));
      d[7] = s.rolling;
      break;
    case 0x102:
      d[0] = (uint8_t)(s.temp + 40);
      d[1] = (uint8_t)s.fuel;
      put16(d + 2, (uint16_t)(s.batt * 1000.0f));
      f.dlc = 4;
      break;
    case 0x103:
      d[0] = s.lights;
      f.dlc = 1;
      break;
    case 0x1A0: {  // velocidades de rueda (4 x uint16, 0.01 km/h)
      for (int i = 0; i < 4; i++) {
        float w = s.speed > 0.5f ? s.speed + frand(-0.2f, 0.2f) : 0.0f;
        put16(d + i * 2, (uint16_t)(w * 100.0f));
      }
      break;
    }
    case 0x2C4: {  // ángulo de volante (int16, 0.1°)
      int16_t a = (int16_t)(s.steer * 10);
      put16(d, (uint16_t)a);
      f.dlc = 2;
      break;
    }
    case 0x3E8:  // odómetro
      d[0] = s.odometer >> 24; d[1] = s.odometer >> 16; d[2] = s.odometer >> 8; d[3] = s.odometer;
      f.dlc = 4;
      break;
    case 0x4F0:  // climatización (temp exterior, consigna, ventilador)
      d[0] = 22 + 40; d[1] = 21 * 2; d[2] = 3;
      f.dlc = 3;
      break;
    case 0x0F0:  // ABS / ESP estado
      d[0] = 0x00; d[1] = (uint8_t)(s.speed > 1); d[7] = s.rolling;
      break;
    case 0x7E8: {  // respuesta OBD-II modo 01, alternando PIDs
      static const uint8_t pids[] = {0x0C, 0x0D, 0x05};
      uint8_t pid = pids[s.obdPid++ % 3];
      d[1] = 0x41;
      d[2] = pid;
      if (pid == 0x0C) { uint16_t v = (uint16_t)(s.rpm * 4); d[0] = 4; d[3] = v >> 8; d[4] = v & 0xFF; }
      else if (pid == 0x0D) { d[0] = 3; d[3] = (uint8_t)(s.speed > 255 ? 255 : s.speed); }
      else { d[0] = 3; d[3] = (uint8_t)(s.temp + 40); }
      for (int i = d[0] + 1; i < 8; i++) d[i] = 0x55;  // relleno ISO-TP
      break;
    }
  }
  canProcessFrame(f, now, valuesOnly);
}

}  // namespace

void simulatorBegin() {
  uint32_t now = millis();
  s.lastPhys = now;
  s.phaseEnd = now + 3000;
  s.target = 0;
  for (auto& e : sched) e.next = now + e.next;
}

void simulatorTick(uint32_t now, bool valuesOnly) {
  float dt = (now - s.lastPhys) / 1000.0f;
  if (dt >= 0.02f) {
    if (dt > 0.5f) dt = 0.5f;
    physics(now, dt);
    s.lastPhys = now;
  }
  for (auto& e : sched) {
    if ((int32_t)(now - e.next) >= 0) {
      e.next += e.periodMs;
      if ((int32_t)(now - e.next) > 1000) e.next = now + e.periodMs;  // tras una pausa larga
      if (e.id == 0x100) s.rolling = (s.rolling + 1) & 0x0F;
      emit(e.id, now, valuesOnly);
    }
  }
}
