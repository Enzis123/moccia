#include "net.h"
#include <WiFi.h>
#include <ESPmDNS.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

static const char* AP_SSID = "MocciaCAN";
static const char* AP_PASS = "moccia1234";
static bool s_ap = false;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static NetInfo s_info = {};

static void startMdns() {
  MDNS.end();
  if (MDNS.begin("moccia")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println(F("[NET] mDNS: http://moccia.local"));
  }
}

static void startAp() {
  s_ap = true;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[NET] AP %s activo, IP %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  startMdns();
}

void netBegin() {
  WiFi.persistent(false);
  WiFi.setHostname("moccia");
  if (strlen(WIFI_SSID) == 0) {
    Serial.println(F("[NET] sin credenciales (secrets.h); modo AP"));
    startAp();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[NET] conectando a %s", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[NET] STA conectado, IP %s\n", WiFi.localIP().toString().c_str());
    startMdns();
  } else {
    Serial.println(F("[NET] STA falló; modo AP"));
    startAp();
  }
}

void netLoop() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 1000) return;
  last = now;
  NetInfo n = {};
  n.ap = s_ap;
  if (s_ap) {
    strlcpy(n.ip, WiFi.softAPIP().toString().c_str(), sizeof(n.ip));
    strlcpy(n.ssid, AP_SSID, sizeof(n.ssid));
    n.rssi = 0;
    n.connected = true;
  } else {
    n.connected = WiFi.status() == WL_CONNECTED;
    strlcpy(n.ip, n.connected ? WiFi.localIP().toString().c_str() : "0.0.0.0", sizeof(n.ip));
    strlcpy(n.ssid, WIFI_SSID, sizeof(n.ssid));
    n.rssi = n.connected ? WiFi.RSSI() : 0;
  }
  portENTER_CRITICAL(&s_mux);
  s_info = n;
  portEXIT_CRITICAL(&s_mux);
}

NetInfo netInfo() {
  portENTER_CRITICAL(&s_mux);
  NetInfo n = s_info;
  portEXIT_CRITICAL(&s_mux);
  return n;
}
