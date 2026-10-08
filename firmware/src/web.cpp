#include "web.h"
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <string>
#include "app_config.h"
#include "state.h"
#include "app_settings.h"
#include "wifi_net.h"

static AsyncWebServer s_server(80);
static AsyncWebSocket s_ws("/ws");
static bool s_fsOk = false;

// ------------------------------------------------------------------ JSON
static double round1(double x) { return round(x * 10.0) / 10.0; }
static double round2(double x) { return round(x * 100.0) / 100.0; }

static void fillState(JsonObject d) {
  VehicleData v;
  int fps;
  BusState bus;
  bool demo, paused;
  {
    StateGuard g;
    v = g_state.v;
    fps = g_state.fps;
    bus = g_state.bus;
    demo = g_state.settings.demo;
    paused = g_state.paused;
  }
  d["rpm"] = v.rpm;
  d["speed"] = round1(v.speed);
  d["temp"] = v.temp;
  d["fuel"] = v.fuel;
  d["batt"] = round2(v.batt);
  d["gear"] = gearStr(v.gear);
  d["lights"] = v.lights;
  d["fps"] = fps;
  d["bus"] = busStr(bus);
  d["demo"] = demo;
  d["paused"] = paused;
  d["uptime"] = uptimeSeconds();
}

static void fillSettings(JsonObject d) {
  AppSettings s = settingsGet();
  d["bitrate"] = s.bitrate;
  d["demo"] = s.demo;
  d["backlight"] = s.backlight;
  d["units"] = s.mph ? "mph" : "kmh";
}

static void fillFrames(JsonArray arr) {
  FrameEntry fr[FRAME_TABLE_SIZE];
  int n = stateSnapshotFrames(fr);
  char id[12], data[8 * 3 + 1];
  for (int i = 0; i < n; i++) {
    const FrameEntry& f = fr[i];
    snprintf(id, sizeof(id), f.ext ? "0x%08X" : "0x%03X", (unsigned)f.id);
    char* p = data;
    *p = 0;
    for (int b = 0; b < f.dlc; b++) p += sprintf(p, b ? " %02X" : "%02X", f.data[b]);
    JsonObject o = arr.add<JsonObject>();
    o["id"] = id;
    o["dlc"] = f.dlc;
    o["data"] = data;
    o["count"] = f.count;
    o["period"] = f.period;
  }
}

static void fillSys(JsonObject d) {
  NetInfo n = netInfo();
  d["ip"] = n.ip;
  d["ssid"] = n.ssid;
  d["rssi"] = n.rssi;
  d["heap"] = ESP.getFreeHeap();
  d["psram"] = ESP.getFreePsram();
  d["ver"] = FW_VERSION;
  d["mode"] = n.ap ? "AP" : "STA";
}

static void fillHistory(JsonObject d) {
  float sp[HISTORY_LEN];
  int16_t rp[HISTORY_LEN], tp[HISTORY_LEN];
  stateCopyHistory(sp, rp, tp);
  JsonArray a = d["speed"].to<JsonArray>();
  for (int i = 0; i < HISTORY_LEN; i++) a.add(round1(sp[i]));
  a = d["rpm"].to<JsonArray>();
  for (int i = 0; i < HISTORY_LEN; i++) a.add(rp[i]);
  a = d["temp"].to<JsonArray>();
  for (int i = 0; i < HISTORY_LEN; i++) a.add(tp[i]);
}

static void fillSample(JsonObject d) {
  StateGuard g;
  int idx = (g_state.histHead + HISTORY_LEN - 1) % HISTORY_LEN;
  d["speed"] = round1(g_state.histSpeed[idx]);
  d["rpm"] = g_state.histRpm[idx];
  d["temp"] = g_state.histTemp[idx];
}

enum class Msg { State, Frames, History, Sample, Settings, Sys };

static String buildMsg(Msg m) {
  JsonDocument doc;
  switch (m) {
    case Msg::State:    doc["t"] = "state";    fillState(doc["d"].to<JsonObject>()); break;
    case Msg::Frames:   doc["t"] = "frames";   fillFrames(doc["d"].to<JsonArray>()); break;
    case Msg::History:  doc["t"] = "history";  fillHistory(doc["d"].to<JsonObject>()); break;
    case Msg::Sample:   doc["t"] = "sample";   fillSample(doc["d"].to<JsonObject>()); break;
    case Msg::Settings: doc["t"] = "settings"; fillSettings(doc["d"].to<JsonObject>()); break;
    case Msg::Sys:      doc["t"] = "sys";      fillSys(doc["d"].to<JsonObject>()); break;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

// ------------------------------------------------------------------ aplicar cambios
// Aplica un ajuste {k, v}. Devuelve false si la clave/valor no es válido.
static bool applySetting(const char* k, JsonVariantConst v) {
  if (!k) return false;
  auto asBool = [](JsonVariantConst x) -> bool {
    if (x.is<bool>()) return x.as<bool>();
    if (x.is<int>()) return x.as<int>() != 0;
    if (x.is<const char*>()) {
      const char* s = x.as<const char*>();
      return !strcmp(s, "true") || !strcmp(s, "on") || !strcmp(s, "1");
    }
    return false;
  };
  if (!strcmp(k, "bitrate")) {
    uint32_t br = v.is<const char*>() ? (uint32_t)atol(v.as<const char*>()) : v.as<uint32_t>();
    return settingsSetBitrate(br);
  }
  if (!strcmp(k, "demo"))      { settingsSetDemo(asBool(v)); return true; }
  if (!strcmp(k, "backlight")) { settingsSetBacklight(asBool(v)); return true; }
  if (!strcmp(k, "units")) {
    const char* u = v.as<const char*>();
    if (!u) return false;
    if (!strcmp(u, "mph")) settingsSetMph(true);
    else if (!strcmp(u, "kmh")) settingsSetMph(false);
    else return false;
    return true;
  }
  return false;
}

static void handleWsMessage(const char* txt, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, txt, len)) return;
  const char* t = doc["t"];
  if (!t) return;
  if (!strcmp(t, "set")) {
    applySetting(doc["k"], doc["v"]);
  } else if (!strcmp(t, "cmd")) {
    const char* c = doc["c"];
    if (!c) return;
    if (!strcmp(c, "pause")) stateSetPaused(true);
    else if (!strcmp(c, "resume")) stateSetPaused(false);
    else if (!strcmp(c, "clear")) stateClearFrames();
  }
  // La difusión de settings/state la hace la tarea webTask al ver cambiar las revisiones.
}

// Búfer de recepción por cliente (en client->_tempObject) para mensajes fragmentados.
static constexpr size_t WS_RX_MAX = 1024;   // los mensajes del protocolo ocupan < 100 bytes
struct WsRxBuf {
  std::string buf;
  bool overflow = false;
};

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type,
                      void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("[WS] cliente #%u conectado (%u en total)\n", (unsigned)client->id(), (unsigned)server->count());
      client->text(buildMsg(Msg::Settings));
      client->text(buildMsg(Msg::Sys));
      client->text(buildMsg(Msg::History));
      client->text(buildMsg(Msg::State));
      client->text(buildMsg(Msg::Frames));
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("[WS] cliente #%u desconectado\n", (unsigned)client->id());
      delete static_cast<WsRxBuf*>(client->_tempObject);
      client->_tempObject = nullptr;
      break;
    case WS_EVT_DATA: {
      AwsFrameInfo* info = (AwsFrameInfo*)arg;
      if (info->message_opcode != WS_TEXT) break;
      // Caso habitual: mensaje completo en una sola trama WS y un solo segmento TCP
      if (info->final && info->num == 0 && info->index == 0 && info->len == len) {
        handleWsMessage((const char*)data, len);
        break;
      }
      // Mensaje fragmentado (varias tramas WS) o trama partida en varios segmentos TCP: acumular
      auto* rx = static_cast<WsRxBuf*>(client->_tempObject);
      if (!rx) client->_tempObject = rx = new WsRxBuf();
      if (info->num == 0 && info->index == 0) { rx->buf.clear(); rx->overflow = false; }
      if (rx->buf.size() + len > WS_RX_MAX) rx->overflow = true;
      if (!rx->overflow) rx->buf.append((const char*)data, len);
      if (info->final && info->index + len == info->len) {
        if (!rx->overflow) handleWsMessage(rx->buf.data(), rx->buf.size());
        rx->buf.clear();
        rx->overflow = false;
      }
      break;
    }
    default:
      break;
  }
}

// ------------------------------------------------------------------ tarea de difusión
static void webTask(void*) {
  uint32_t lastState = 0, lastFrames = 0, lastSys = 0, lastClean = 0;
  uint32_t seenSettings = 0, seenControl = 0, seenSample = 0;
  {
    StateGuard g;
    seenSettings = g_state.settingsRev;
    seenControl = g_state.controlRev;
    seenSample = g_state.sampleRev;
  }
  for (;;) {
    uint32_t now = millis();
    netLoop();

    uint32_t settingsRev, controlRev, sampleRev;
    {
      StateGuard g;
      settingsRev = g_state.settingsRev;
      controlRev = g_state.controlRev;
      sampleRev = g_state.sampleRev;
    }

    if (s_ws.count() > 0) {
      bool forceState = false;
      if (settingsRev != seenSettings) {
        seenSettings = settingsRev;
        s_ws.textAll(buildMsg(Msg::Settings));
        forceState = true;
      }
      if (controlRev != seenControl) {
        seenControl = controlRev;
        forceState = true;
        s_ws.textAll(buildMsg(Msg::Frames));
        lastFrames = now;
      }
      bool canWrite = s_ws.availableForWriteAll();
      if (forceState || (canWrite && now - lastState >= 100)) {
        lastState = now;
        s_ws.textAll(buildMsg(Msg::State));
      }
      if (canWrite && now - lastFrames >= 500) {
        lastFrames = now;
        s_ws.textAll(buildMsg(Msg::Frames));
      }
      if (sampleRev != seenSample && canWrite) {   // si la cola está llena se reintenta en 10 ms
        seenSample = sampleRev;
        s_ws.textAll(buildMsg(Msg::Sample));
      }
      if (now - lastSys >= 5000) {
        lastSys = now;
        s_ws.textAll(buildMsg(Msg::Sys));
      }
    } else {
      seenSettings = settingsRev;
      seenControl = controlRev;
      seenSample = sampleRev;
    }

    if (now - lastClean >= 1000) {
      lastClean = now;
      s_ws.cleanupClients();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ------------------------------------------------------------------ HTTP
static const char FALLBACK_HTML[] PROGMEM =
    "<!doctype html><html lang=es><meta charset=utf-8><title>MocciaCAN</title>"
    "<body style='background:#0f1115;color:#e6e6e6;font-family:sans-serif;padding:2em'>"
    "<h1 style='color:#00c2ff'>MocciaCAN</h1><p>La app web no está en LittleFS.</p>"
    "<p>Súbela con <code>pio run -t uploadfs</code>. API: <a href=/api/state>/api/state</a>, "
    "<a href=/api/settings>/api/settings</a>, WebSocket <code>/ws</code>.</p></body></html>";

static void sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  req->send(code, "application/json", out);
}

void webBegin() {
  s_fsOk = LittleFS.begin(false);
  if (!s_fsOk) Serial.println(F("[WEB] LittleFS no montado (¿falta uploadfs?)"));

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");

  s_ws.onEvent(onWsEvent);
  s_server.addHandler(&s_ws);

  s_server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    fillState(doc.to<JsonObject>());
    sendJson(req, doc);
  });

  s_server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    fillSettings(doc.to<JsonObject>());
    sendJson(req, doc);
  });

  auto* post = new AsyncCallbackJsonWebHandler("/api/settings", [](AsyncWebServerRequest* req, JsonVariant& json) {
    JsonObjectConst obj = json.as<JsonObjectConst>();
    if (obj.isNull()) {
      req->send(400, "application/json", "{\"error\":\"se esperaba un objeto JSON\"}");
      return;
    }
    bool ok = true;
    for (JsonPairConst kv : obj) ok &= applySetting(kv.key().c_str(), kv.value());
    JsonDocument doc;
    fillSettings(doc.to<JsonObject>());
    sendJson(req, doc, ok ? 200 : 400);
  });
  post->setMethod(HTTP_POST);
  s_server.addHandler(post);

  if (s_fsOk && LittleFS.exists("/index.html")) {
    // index.html, app.css, app.js, manifest.json, icon.svg...
    s_server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");
  } else {
    s_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) { req->send(200, "text/html", FALLBACK_HTML); });
  }

  s_server.onNotFound([](AsyncWebServerRequest* req) {
    if (req->method() == HTTP_OPTIONS) req->send(204);
    else req->send(404, "text/plain", "No encontrado");
  });

  s_server.begin();
  Serial.println(F("[WEB] servidor HTTP en puerto 80, WebSocket /ws"));

  xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 2, nullptr, 0);
}
