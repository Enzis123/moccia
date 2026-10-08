// MocciaCAN — panel de vehículo / monitor CAN para Waveshare ESP32-S3-Touch-LCD-7
//
// Tareas:
//   loopTask (Arduino, núcleo 1) : UI — táctil, dibujo y CH422G (único dueño del bus I2C)
//   "can"    (núcleo 0, prio 5)  : recepción TWAI, recuperación BUS-OFF, simulador, tramas/s, historial
//   "web"    (núcleo 0, prio 2)  : difusión WebSocket (10 Hz estado, 2 Hz tramas, 1 Hz muestra, sys 5 s)
//   async_tcp (núcleo 0)         : peticiones HTTP / mensajes WebSocket entrantes
//   "netinit" (una vez)          : conexión WiFi (hasta 10 s) y arranque del servidor
#include <Arduino.h>
#include "app_config.h"
#include "state.h"
#include "app_settings.h"
#include "ch422g.h"
#include "ui.h"
#include "can_bus.h"
#include "wifi_net.h"
#include "web.h"

static void netInitTask(void*) {
  netBegin();
  webBegin();
  vTaskDelete(nullptr);
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.printf("\n[%s] firmware %s\n", FW_NAME, FW_VERSION);
  Serial.printf("[SYS] PSRAM: %u bytes libres\n", (unsigned)ESP.getFreePsram());

  stateInit();
  settingsLoad();
  AppSettings st = settingsGet();

  // El CH422G debe configurarse ANTES de lcd.init(): resets, retroiluminación y USB_SEL=1 (CAN)
  if (!ch422gBegin(st.backlight)) Serial.println(F("[SYS] CH422G no responde"));

  uiBegin();
  canBegin(st.bitrate);

  // La conexión WiFi puede tardar hasta 10 s: se hace en segundo plano para no bloquear la UI
  xTaskCreatePinnedToCore(netInitTask, "netinit", 6144, nullptr, 3, nullptr, 0);
}

void loop() {
  uiLoop();
  vTaskDelay(pdMS_TO_TICKS(2));
}
