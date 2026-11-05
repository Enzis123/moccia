// =====================================================================
//  MocciaCAN - version simple
//  Placa: Waveshare ESP32-S3-Touch-LCD-7 (pantalla 800x480 tactil + CAN)
//
//  - Pantalla tactil con 3 paginas: Tablero, CAN y Ajustes.
//  - Crea su propia red WiFi y sirve una pagina web que muestra lo mismo.
//  - Lee el bus CAN a 500 kbit/s en modo "solo escucha" (no envia nada).
//  - Modo demo: inventa datos para probarlo sin coche.
//
//  Unica libreria necesaria: LovyanGFX (version 1.1.16).
// =====================================================================

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include "driver/twai.h"
#include "pagina_web.h"

// ---------------- Cosas que puedes cambiar ----------------
const char* WIFI_NOMBRE = "MocciaCAN";
const char* WIFI_CLAVE  = "moccia1234";   // minimo 8 caracteres
const char* VERSION     = "2.0";

// ---------------- Colores ----------------
const uint32_t C_FONDO   = 0x0f1115u;
const uint32_t C_TARJETA = 0x1a1d24u;
const uint32_t C_ACENTO  = 0x00c2ffu;
const uint32_t C_OK      = 0x2ecc71u;
const uint32_t C_TEXTO   = 0xe6e6e6u;
const uint32_t C_GRIS    = 0x8a8f98u;
const uint32_t C_LINEA   = 0x2a2e37u;

// =====================================================================
//  Pantalla (pines de la placa Waveshare de 7")
// =====================================================================
class LGFX : public lgfx::LGFX_Device {
 public:
  lgfx::Bus_RGB     _bus;
  lgfx::Panel_RGB   _panel;
  lgfx::Touch_GT911 _touch;

  LGFX() {
    {
      auto cfg = _panel.config();
      cfg.memory_width  = cfg.panel_width  = 800;
      cfg.memory_height = cfg.panel_height = 480;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.use_psram = 1;
      _panel.config_detail(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;
      cfg.pin_d0  = 14; cfg.pin_d1  = 38; cfg.pin_d2  = 18; cfg.pin_d3  = 17; cfg.pin_d4  = 10;  // azul
      cfg.pin_d5  = 39; cfg.pin_d6  = 0;  cfg.pin_d7  = 45; cfg.pin_d8  = 48; cfg.pin_d9  = 47;  // verde
      cfg.pin_d10 = 21;
      cfg.pin_d11 = 1;  cfg.pin_d12 = 2;  cfg.pin_d13 = 42; cfg.pin_d14 = 41; cfg.pin_d15 = 40;  // rojo
      cfg.pin_henable = 5;
      cfg.pin_vsync   = 3;
      cfg.pin_hsync   = 46;
      cfg.pin_pclk    = 7;
      cfg.freq_write  = 16000000;   // si la imagen tiembla, prueba 14000000
      cfg.hsync_polarity = 0; cfg.hsync_front_porch = 8; cfg.hsync_pulse_width = 4; cfg.hsync_back_porch = 8;
      cfg.vsync_polarity = 0; cfg.vsync_front_porch = 8; cfg.vsync_pulse_width = 4; cfg.vsync_back_porch = 8;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high    = 0;
      cfg.pclk_idle_high  = 0;
      _bus.config(cfg);
    }
    _panel.setBus(&_bus);
    {
      auto cfg = _touch.config();
      cfg.x_min = 0; cfg.x_max = 799;
      cfg.y_min = 0; cfg.y_max = 479;
      cfg.pin_int  = 4;
      cfg.pin_rst  = -1;      // el reset lo hace el CH422G
      cfg.i2c_port = 0;
      cfg.pin_sda  = 8;
      cfg.pin_scl  = 9;
      cfg.freq     = 400000;
      cfg.i2c_addr = 0x5D;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

LGFX lcd;
LGFX_Sprite lienzo(&lcd);
WebServer servidor(80);

// =====================================================================
//  Datos
// =====================================================================
struct Trama {
  uint32_t id;
  uint8_t  largo;
  uint8_t  datos[8];
  uint32_t veces;
};
const int MAX_TRAMAS = 8;
Trama tramas[MAX_TRAMAS];
int numTramas = 0;

float velocidad   = 0;   // km/h
int   rpm         = 0;
int   temperatura = 0;   // grados C
float bateria     = 0;   // voltios
bool  modoDemo    = true;

uint32_t tramasTotales  = 0;
uint32_t tramasPorSeg   = 0;
uint32_t contadorSeg    = 0;

int  pagina = 0;          // 0 = Tablero, 1 = CAN, 2 = Ajustes
bool redibujarTodo = true;

// =====================================================================
//  CH422G: expansor que enciende la pantalla, el tactil y activa el CAN
// =====================================================================
void iniciarExpansor() {
  Wire.begin(8, 9);
  Wire.beginTransmission(0x24); Wire.write(0x01); Wire.endTransmission();  // modo salida

  // bit1 = reset tactil, bit2 = luz, bit3 = reset pantalla, bit4 = SD, bit5 = CAN
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);                                                     // direccion tactil 0x5D
  Wire.beginTransmission(0x38); Wire.write(0b00110000); Wire.endTransmission();
  delay(20);
  Wire.beginTransmission(0x38); Wire.write(0b00111000); Wire.endTransmission();
  delay(10);
  Wire.beginTransmission(0x38); Wire.write(0b00111110); Wire.endTransmission();  // todo encendido
  delay(60);
  pinMode(4, INPUT);
  delay(50);
  Wire.end();   // a partir de aqui el I2C lo usa la pantalla para el tactil
}

// =====================================================================
//  CAN
// =====================================================================
void iniciarCAN() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_20, GPIO_NUM_19, TWAI_MODE_LISTEN_ONLY);
  twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) == ESP_OK && twai_start() == ESP_OK) {
    Serial.println("CAN listo a 500 kbit/s");
  } else {
    Serial.println("Error al iniciar el CAN");
  }
}

// Guarda la trama en la tabla y saca de ella los valores que conocemos
void procesarTrama(uint32_t id, uint8_t largo, const uint8_t* d) {
  tramasTotales++;
  contadorSeg++;

  int i = 0;
  while (i < numTramas && tramas[i].id != id) i++;
  if (i == numTramas) {
    if (numTramas < MAX_TRAMAS) numTramas++;
    else i = MAX_TRAMAS - 1;   // tabla llena: reemplaza la ultima
    tramas[i].id = id;
    tramas[i].veces = 0;
  }
  tramas[i].largo = largo;
  memcpy(tramas[i].datos, d, largo);
  tramas[i].veces++;

  if (id == 0x100 && largo >= 2) rpm = (d[0] << 8) | d[1];
  if (id == 0x101 && largo >= 2) velocidad = ((d[0] << 8) | d[1]) / 100.0;
  if (id == 0x102 && largo >= 4) {
    temperatura = d[0] - 40;
    bateria = ((d[2] << 8) | d[3]) / 1000.0;
  }
}

void leerCAN() {
  twai_message_t m;
  while (twai_receive(&m, 0) == ESP_OK) {
    if (!modoDemo) procesarTrama(m.identifier, m.data_length_code, m.data);
  }
}

// Modo demo: inventa un coche circulando y lo manda por el mismo camino que el CAN real
void simular() {
  float t = millis() / 1000.0;
  float v = 60 + 50 * sin(t / 8);
  int   r = 900 + v * 28 + 200 * sin(t * 1.3);
  int   c = min(90, 40 + (int)(t / 2));
  int   mv = 13900 + 200 * sin(t / 5);

  uint16_t vv = v * 100;
  uint8_t a[8] = { (uint8_t)(r >> 8), (uint8_t)r, 3, 0, 0, 0, 0, 0 };
  uint8_t b[8] = { (uint8_t)(vv >> 8), (uint8_t)vv, 0, 0, 0, 0, 0, 0 };
  uint8_t e[8] = { (uint8_t)(c + 40), 70, (uint8_t)(mv >> 8), (uint8_t)mv, 0, 0, 0, 0 };
  procesarTrama(0x100, 8, a);
  procesarTrama(0x101, 8, b);
  procesarTrama(0x102, 8, e);
}

String datosHex(const Trama& t) {
  String s;
  char h[4];
  for (int i = 0; i < t.largo; i++) {
    sprintf(h, "%02X ", t.datos[i]);
    s += h;
  }
  s.trim();
  return s;
}

String tiempoEncendido() {
  uint32_t s = millis() / 1000;
  char txt[16];
  sprintf(txt, "%02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  return txt;
}

// =====================================================================
//  Dibujo
// =====================================================================
// Escribe un texto dentro de una caja (sin parpadeo: se dibuja aparte y se pega)
void texto(const String& s, int x, int y, int w, int h, const lgfx::IFont* fuente,
           uint32_t color, uint32_t fondo, bool centrado) {
  lienzo.createSprite(w, h);
  lienzo.fillSprite(fondo);
  lienzo.setFont(fuente);
  lienzo.setTextColor(color);
  if (centrado) {
    lienzo.setTextDatum(lgfx::textdatum_t::middle_center);
    lienzo.drawString(s, w / 2, h / 2);
  } else {
    lienzo.setTextDatum(lgfx::textdatum_t::middle_left);
    lienzo.drawString(s, 0, h / 2);
  }
  lienzo.pushSprite(x, y);
  lienzo.deleteSprite();
}

void barra(int x, int y, int w, float fraccion) {
  fraccion = constrain(fraccion, 0, 1);
  int lleno = w * fraccion;
  lcd.fillRect(x, y, lleno, 12, C_ACENTO);
  lcd.fillRect(x + lleno, y, w - lleno, 12, C_LINEA);
}

const char* NOMBRES[3] = { "Tablero", "CAN", "Ajustes" };

void dibujarBarraSuperior() {
  lcd.fillRect(0, 0, 800, 60, C_TARJETA);
  texto("MocciaCAN", 20, 10, 200, 40, &fonts::FreeSansBold18pt7b, C_TEXTO, C_TARJETA, false);
  for (int i = 0; i < 3; i++) {
    int x = 310 + i * 160;
    bool activa = (i == pagina);
    uint32_t fondo = activa ? C_ACENTO : C_FONDO;
    lcd.fillRoundRect(x, 8, 150, 44, 10, fondo);
    texto(NOMBRES[i], x + 10, 12, 130, 36, &fonts::FreeSansBold12pt7b, activa ? C_FONDO : C_GRIS, fondo, true);
  }
}

// --- Tablero: 4 tarjetas ---
const int TX[4] = { 20, 410, 20, 410 };
const int TY[4] = { 75, 75, 280, 280 };

void dibujarTableroFijo() {
  const char* titulos[4] = { "Velocidad", "Revoluciones", "Temperatura", "Bateria" };
  const char* unidades[4] = { "km/h", "rpm", "grados C", "voltios" };
  for (int i = 0; i < 4; i++) {
    lcd.fillRoundRect(TX[i], TY[i], 370, 190, 14, C_TARJETA);
    texto(titulos[i], TX[i] + 20, TY[i] + 12, 250, 30, &fonts::FreeSansBold12pt7b, C_GRIS, C_TARJETA, false);
    texto(unidades[i], TX[i] + 250, TY[i] + 12, 100, 30, &fonts::FreeSans12pt7b, C_GRIS, C_TARJETA, false);
  }
}

void dibujarTableroValores() {
  String valores[4] = { String((int)velocidad), String(rpm), String(temperatura), String(bateria, 1) };
  float fracciones[4] = { velocidad / 240, rpm / 8000.0f, temperatura / 120.0f, (bateria - 11) / 4 };
  for (int i = 0; i < 4; i++) {
    texto(valores[i], TX[i] + 20, TY[i] + 50, 330, 100, &fonts::DejaVu72, C_TEXTO, C_TARJETA, true);
    barra(TX[i] + 20, TY[i] + 160, 330, fracciones[i]);
  }
}

// --- CAN: tabla de tramas ---
void dibujarCANFijo() {
  lcd.fillRoundRect(20, 75, 760, 390, 14, C_TARJETA);
  int y = 125;
  texto("ID",    40,  y, 100, 30, &fonts::FreeSansBold12pt7b, C_GRIS, C_TARJETA, false);
  texto("Bytes", 150, y, 90,  30, &fonts::FreeSansBold12pt7b, C_GRIS, C_TARJETA, false);
  texto("Datos", 250, y, 380, 30, &fonts::FreeSansBold12pt7b, C_GRIS, C_TARJETA, false);
  texto("Veces", 640, y, 120, 30, &fonts::FreeSansBold12pt7b, C_GRIS, C_TARJETA, false);
  lcd.drawFastHLine(40, 158, 720, C_LINEA);
}

void dibujarCANValores() {
  String resumen = "Tramas por segundo: " + String(tramasPorSeg) + "     Total: " + String(tramasTotales);
  texto(resumen, 40, 85, 720, 34, &fonts::FreeSansBold12pt7b, C_ACENTO, C_TARJETA, false);

  for (int i = 0; i < MAX_TRAMAS; i++) {
    int y = 165 + i * 36;
    if (i < numTramas) {
      char id[12];
      sprintf(id, "0x%03lX", (unsigned long)tramas[i].id);
      texto(id,                          40,  y, 100, 32, &fonts::FreeMonoBold12pt7b, C_TEXTO, C_TARJETA, false);
      texto(String(tramas[i].largo),     150, y, 90,  32, &fonts::FreeMonoBold12pt7b, C_TEXTO, C_TARJETA, false);
      texto(datosHex(tramas[i]),         250, y, 380, 32, &fonts::FreeMonoBold12pt7b, C_TEXTO, C_TARJETA, false);
      texto(String(tramas[i].veces),     640, y, 120, 32, &fonts::FreeMonoBold12pt7b, C_TEXTO, C_TARJETA, false);
    } else if (i == 0) {
      texto("Esperando tramas CAN (500 kbit/s)...", 40, y, 720, 32, &fonts::FreeSans12pt7b, C_GRIS, C_TARJETA, false);
    } else {
      lcd.fillRect(40, y, 720, 32, C_TARJETA);
    }
  }
}

// --- Ajustes ---
void dibujarBotonDemo() {
  uint32_t fondo = modoDemo ? C_OK : C_LINEA;
  lcd.fillRoundRect(20, 75, 370, 100, 14, fondo);
  texto(modoDemo ? "Modo demo: SI" : "Modo demo: NO", 30, 95, 350, 60,
        &fonts::FreeSansBold18pt7b, modoDemo ? C_FONDO : C_TEXTO, fondo, true);
}

void dibujarAjustesFijo() {
  dibujarBotonDemo();
  lcd.fillRoundRect(20, 190, 370, 275, 14, C_TARJETA);
  texto("Toca el boton verde para",    40, 210, 330, 30, &fonts::FreeSans12pt7b, C_GRIS, C_TARJETA, false);
  texto("activar o quitar los datos",  40, 240, 330, 30, &fonts::FreeSans12pt7b, C_GRIS, C_TARJETA, false);
  texto("simulados.",                  40, 270, 330, 30, &fonts::FreeSans12pt7b, C_GRIS, C_TARJETA, false);
  texto("Quitalo para ver el CAN real.", 40, 320, 330, 30, &fonts::FreeSans12pt7b, C_GRIS, C_TARJETA, false);

  lcd.fillRoundRect(410, 75, 370, 390, 14, C_TARJETA);
  texto("App web", 430, 90, 330, 34, &fonts::FreeSansBold12pt7b, C_ACENTO, C_TARJETA, false);
  texto(String("Red WiFi: ") + WIFI_NOMBRE, 430, 130, 330, 30, &fonts::FreeSans12pt7b, C_TEXTO, C_TARJETA, false);
  texto(String("Clave: ") + WIFI_CLAVE,     430, 165, 330, 30, &fonts::FreeSans12pt7b, C_TEXTO, C_TARJETA, false);
  texto("Abre: http://" + WiFi.softAPIP().toString(), 430, 200, 330, 30, &fonts::FreeSans12pt7b, C_TEXTO, C_TARJETA, false);
  texto("Sistema", 430, 260, 330, 34, &fonts::FreeSansBold12pt7b, C_ACENTO, C_TARJETA, false);
  texto("CAN: 500 kbit/s, solo escucha", 430, 300, 330, 30, &fonts::FreeSans12pt7b, C_TEXTO, C_TARJETA, false);
  texto(String("Version: ") + VERSION, 430, 335, 330, 30, &fonts::FreeSans12pt7b, C_TEXTO, C_TARJETA, false);
}

void dibujarAjustesValores() {
  texto("Encendido: " + tiempoEncendido(), 430, 370, 330, 30, &fonts::FreeSans12pt7b, C_TEXTO, C_TARJETA, false);
}

void dibujarPagina() {
  if (redibujarTodo) {
    lcd.fillRect(0, 60, 800, 420, C_FONDO);
    dibujarBarraSuperior();
    if (pagina == 0) dibujarTableroFijo();
    if (pagina == 1) dibujarCANFijo();
    if (pagina == 2) dibujarAjustesFijo();
    redibujarTodo = false;
  }
  if (pagina == 0) dibujarTableroValores();
  if (pagina == 1) dibujarCANValores();
  if (pagina == 2) dibujarAjustesValores();
}

// =====================================================================
//  Tactil
// =====================================================================
void cambiarDemo(bool activar) {
  modoDemo = activar;
  numTramas = 0;
  if (pagina == 2) dibujarBotonDemo();
}

void revisarTactil() {
  static bool tocando = false;
  int32_t x, y;
  bool ahora = lcd.getTouch(&x, &y);
  if (ahora && !tocando) {             // solo al apoyar el dedo
    if (y < 60 && x >= 310) {          // pestanas
      int p = (x - 310) / 160;
      if (p >= 0 && p < 3 && p != pagina) {
        pagina = p;
        redibujarTodo = true;
      }
    } else if (pagina == 2 && x < 390 && y >= 75 && y < 175) {
      cambiarDemo(!modoDemo);
    }
  }
  tocando = ahora;
}

// =====================================================================
//  Pagina web
// =====================================================================
void webInicio() {
  servidor.send_P(200, "text/html", PAGINA_WEB);
}

void webDatos() {
  String j = "{";
  j += "\"vel\":" + String(velocidad, 1);
  j += ",\"rpm\":" + String(rpm);
  j += ",\"temp\":" + String(temperatura);
  j += ",\"bat\":" + String(bateria, 2);
  j += ",\"demo\":" + String(modoDemo ? "true" : "false");
  j += ",\"tps\":" + String(tramasPorSeg);
  j += ",\"total\":" + String(tramasTotales);
  j += ",\"encendido\":\"" + tiempoEncendido() + "\"";
  j += ",\"wifi\":\"" + String(WIFI_NOMBRE) + "\"";
  j += ",\"clave\":\"" + String(WIFI_CLAVE) + "\"";
  j += ",\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
  j += ",\"version\":\"" + String(VERSION) + "\"";
  j += ",\"tramas\":[";
  for (int i = 0; i < numTramas; i++) {
    char id[12];
    sprintf(id, "0x%03lX", (unsigned long)tramas[i].id);
    if (i) j += ",";
    j += "{\"id\":\"" + String(id) + "\",\"largo\":" + String(tramas[i].largo) +
         ",\"datos\":\"" + datosHex(tramas[i]) + "\",\"veces\":" + String(tramas[i].veces) + "}";
  }
  j += "]}";
  servidor.send(200, "application/json", j);
}

void webDemo() {
  cambiarDemo(servidor.arg("on") == "1");
  webDatos();
}

// =====================================================================
//  Programa principal
// =====================================================================
void setup() {
  Serial.begin(115200);

  iniciarExpansor();
  lcd.init();
  lienzo.setColorDepth(16);
  lienzo.setPsram(true);
  lcd.fillScreen(C_FONDO);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_NOMBRE, WIFI_CLAVE);
  servidor.on("/", webInicio);
  servidor.on("/datos", webDatos);
  servidor.on("/demo", webDemo);
  servidor.begin();
  Serial.print("Web en http://");
  Serial.println(WiFi.softAPIP());

  iniciarCAN();
}

void loop() {
  static uint32_t ultSim = 0, ultDibujo = 0, ultSeg = 0, ultTactil = 0;
  uint32_t ahora = millis();

  servidor.handleClient();
  leerCAN();

  if (modoDemo && ahora - ultSim >= 100) { ultSim = ahora; simular(); }
  if (ahora - ultSeg >= 1000)            { ultSeg = ahora; tramasPorSeg = contadorSeg; contadorSeg = 0; }
  if (ahora - ultTactil >= 30)           { ultTactil = ahora; revisarTactil(); }
  if (redibujarTodo || ahora - ultDibujo >= 250) { ultDibujo = ahora; dibujarPagina(); }
}
