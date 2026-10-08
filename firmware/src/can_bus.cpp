#include "can_bus.h"
#include <driver/twai.h>
#include "app_config.h"
#include "state.h"
#include "simulator.h"

static volatile uint32_t s_pendingBitrate = 0;
static volatile bool     s_driverOk = false;
static uint32_t s_curBitrate = 500000;
static uint32_t s_lastFrameMs = 0;      // última trama (real o simulada)
static bool     s_haveFrame = false;
static uint32_t s_lastRealMs = 0;       // última trama real del bus
static bool     s_haveReal = false;
static uint32_t s_fpsCount = 0;
static bool     s_busOff = false;
static TaskHandle_t s_task = nullptr;

// ---------------------------------------------------------------- driver TWAI
static bool installDriver(uint32_t br) {
  twai_general_config_t g =
      TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)PIN_CAN_TX, (gpio_num_t)PIN_CAN_RX, TWAI_MODE_NORMAL);
  g.rx_queue_len = 64;
  g.tx_queue_len = 4;
  g.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED | TWAI_ALERT_ERR_PASS |
                     TWAI_ALERT_RX_QUEUE_FULL;
  g.intr_flags = ESP_INTR_FLAG_LEVEL1;

  twai_timing_config_t t125 = TWAI_TIMING_CONFIG_125KBITS();
  twai_timing_config_t t250 = TWAI_TIMING_CONFIG_250KBITS();
  twai_timing_config_t t500 = TWAI_TIMING_CONFIG_500KBITS();
  twai_timing_config_t t1m  = TWAI_TIMING_CONFIG_1MBITS();
  twai_timing_config_t* t = &t500;
  switch (br) {
    case 125000:  t = &t125; break;
    case 250000:  t = &t250; break;
    case 1000000: t = &t1m;  break;
    default:      t = &t500; br = 500000; break;
  }
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g, t, &f) != ESP_OK) {
    Serial.println(F("[CAN] twai_driver_install falló"));
    return false;
  }
  if (twai_start() != ESP_OK) {
    Serial.println(F("[CAN] twai_start falló"));
    twai_driver_uninstall();
    return false;
  }
  s_curBitrate = br;
  s_busOff = false;
  Serial.printf("[CAN] driver TWAI activo a %u bit/s\n", (unsigned)br);
  return true;
}

static void uninstallDriver() {
  if (!s_driverOk) return;
  s_driverOk = false;
  twai_stop();                 // puede fallar en BUS-OFF; da igual
  twai_driver_uninstall();
}

void canRequestBitrate(uint32_t bitrate) { s_pendingBitrate = bitrate; }

bool canDriverOk() { return s_driverOk; }

// ---------------------------------------------------------------- decodificación
static void decode(const CanFrame& f, VehicleData& v) {
  if (f.ext) return;
  const uint8_t* d = f.data;
  switch (f.id) {
    case 0x100:
      if (f.dlc >= 2) v.rpm = (d[0] << 8) | d[1];
      if (f.dlc >= 3 && d[2] <= 9) v.gear = d[2];
      break;
    case 0x101:
      if (f.dlc >= 2) v.speed = ((d[0] << 8) | d[1]) * 0.01f;
      break;
    case 0x102:
      if (f.dlc >= 1) v.temp = (int)d[0] - 40;
      if (f.dlc >= 2) v.fuel = d[1] > 100 ? 100 : d[1];
      if (f.dlc >= 4) v.batt = ((d[2] << 8) | d[3]) / 1000.0f;
      break;
    case 0x103:
      if (f.dlc >= 1) v.lights = d[0];
      break;
    case 0x7E8:  // respuesta OBD-II: [len, 0x41, PID, A, B, ...]
      if (f.dlc >= 4 && d[1] == 0x41) {
        uint8_t a = d[3];
        uint8_t b = f.dlc >= 5 ? d[4] : 0;
        switch (d[2]) {
          case 0x0C: if (d[0] >= 4) v.rpm = ((a << 8) | b) / 4; break;
          case 0x0D: v.speed = a; break;
          case 0x05: v.temp = (int)a - 40; break;
        }
      }
      break;
  }
}

static void updateTable(const CanFrame& f, uint32_t now) {
  FrameEntry* slot = nullptr;
  FrameEntry* oldest = nullptr;
  for (auto& e : g_state.frames) {
    if (e.used && e.id == f.id && e.ext == f.ext) { slot = &e; break; }
  }
  if (slot) {
    slot->period = now - slot->lastMs;
  } else {
    for (auto& e : g_state.frames) {
      if (!e.used) { slot = &e; break; }
      if (!oldest || e.lastMs < oldest->lastMs) oldest = &e;
    }
    if (!slot) slot = oldest;   // tabla llena: se sustituye la ID menos reciente
    *slot = FrameEntry();
    slot->used = true;
    slot->id = f.id;
    slot->ext = f.ext;
  }
  slot->dlc = f.dlc;
  memset(slot->data, 0, sizeof(slot->data));
  memcpy(slot->data, f.data, f.dlc > 8 ? 8 : f.dlc);
  slot->count++;
  slot->lastMs = now;
  g_state.framesRev++;
}

void canProcessFrame(const CanFrame& f, uint32_t nowMs, bool valuesOnly) {
  StateGuard g;
  decode(f, g_state.v);
  if (valuesOnly) return;
  if (!g_state.paused) updateTable(f, nowMs);
  s_fpsCount++;
  s_lastFrameMs = nowMs;
  s_haveFrame = true;
}

// ---------------------------------------------------------------- tarea
static void canTask(void*) {
  uint32_t lastStatus = 0, lastSecond = millis(), lastRetry = 0;
  for (;;) {
    uint32_t now = millis();

    // Cambio de bitrate solicitado desde pantalla/web
    uint32_t pend = s_pendingBitrate;
    if (pend) {
      s_pendingBitrate = 0;
      if (pend != s_curBitrate || !s_driverOk) {
        uninstallDriver();
        s_driverOk = installDriver(pend);
        if (!s_driverOk) s_curBitrate = pend;
      }
    }

    if (s_driverOk) {
      twai_message_t m;
      int n = 0;
      if (twai_receive(&m, pdMS_TO_TICKS(5)) == ESP_OK) {
        do {
          if (!m.rtr) {
            CanFrame f;
            f.id = m.identifier;
            f.ext = m.extd;
            f.dlc = m.data_length_code > 8 ? 8 : m.data_length_code;
            memcpy(f.data, m.data, 8);
            uint32_t t = millis();
            s_lastRealMs = t;
            s_haveReal = true;
            canProcessFrame(f, t);
          }
        } while (++n < 64 && twai_receive(&m, 0) == ESP_OK);
      }
      uint32_t alerts;
      twai_read_alerts(&alerts, 0);   // vacía la cola de alertas

      if (now - lastStatus >= 100) {
        lastStatus = now;
        twai_status_info_t st;
        if (twai_get_status_info(&st) == ESP_OK) {
          if (st.state == TWAI_STATE_BUS_OFF) {
            if (!s_busOff) Serial.println(F("[CAN] BUS-OFF, iniciando recuperación"));
            s_busOff = true;
            twai_initiate_recovery();
          } else if (st.state == TWAI_STATE_RECOVERING) {
            s_busOff = true;
          } else if (st.state == TWAI_STATE_STOPPED) {
            // recuperación completada => volver a arrancar
            if (twai_start() == ESP_OK) {
              Serial.println(F("[CAN] bus recuperado"));
              s_busOff = false;
            }
          } else {
            s_busOff = false;
          }
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
      if (now - lastRetry > 3000) {   // reintento periódico si el driver no arrancó
        lastRetry = now;
        s_driverOk = installDriver(s_curBitrate);
      }
    }

    now = millis();
    bool demo;
    {
      StateGuard g;
      demo = g_state.settings.demo;
    }
    if (demo) {
      simulatorTick(now, false);          // demo: tramas simuladas completas
    } else if (!s_haveReal || now - s_lastRealMs > 2000) {
      simulatorTick(now, true);           // sin tráfico: el tablero sigue vivo, bus IDLE, 0 tramas/s
    }

    // Estado del bus
    {
      StateGuard g;
      BusState b;
      if (s_busOff) b = BusState::BUSOFF;
      else if (s_haveFrame && (now - s_lastFrameMs) < 1000) b = BusState::OK;
      else b = BusState::IDLE;
      g_state.bus = b;
    }

    // Cada segundo: tramas/s y muestra del historial
    if (now - lastSecond >= 1000) {
      lastSecond += 1000;
      if (now - lastSecond > 3000) lastSecond = now;   // evita ráfagas tras un bloqueo
      {
        StateGuard g;
        g_state.fps = (int)s_fpsCount;
        s_fpsCount = 0;
      }
      statePushSample();
    }
  }
}

void canBegin(uint32_t bitrate) {
  s_curBitrate = bitrate;
  s_driverOk = installDriver(bitrate);
  simulatorBegin();
  xTaskCreatePinnedToCore(canTask, "can", 6144, nullptr, 5, &s_task, 0);
}
