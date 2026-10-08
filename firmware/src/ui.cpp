#include "ui.h"
#include "display.h"
#include "app_config.h"
#include "state.h"
#include "app_settings.h"
#include "ch422g.h"
#include "can_bus.h"
#include "wifi_net.h"

// ============================================================================ básicos
static LGFX lcd;

// Colores RGB888 (uint32_t => LovyanGFX los interpreta como 888)
static constexpr uint32_t C_BG     = COL_BG;
static constexpr uint32_t C_CARD   = COL_CARD;
static constexpr uint32_t C_ACCENT = COL_ACCENT;
static constexpr uint32_t C_OK     = COL_OK;
static constexpr uint32_t C_WARN   = COL_WARN;
static constexpr uint32_t C_ERR    = COL_ERR;
static constexpr uint32_t C_TEXT   = COL_TEXT;
static constexpr uint32_t C_TEXT2  = COL_TEXT2;
static constexpr uint32_t C_LINE   = COL_LINE;
static constexpr uint32_t C_ROWALT = 0x14171cu;

static uint32_t blend(uint32_t a, uint32_t b, float t) {  // t=0 => a, t=1 => b
  auto ch = [&](int sh) {
    int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
    return (uint32_t)(ca + (cb - ca) * t) & 0xFF;
  };
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// Fuentes
static const lgfx::GFXfont* const F_SM   = &fonts::FreeSans9pt7b;
static const lgfx::GFXfont* const F_SMB  = &fonts::FreeSansBold9pt7b;
static const lgfx::GFXfont* const F_MD   = &fonts::FreeSans12pt7b;
static const lgfx::GFXfont* const F_MDB  = &fonts::FreeSansBold12pt7b;
static const lgfx::GFXfont* const F_LGB  = &fonts::FreeSansBold18pt7b;
static const lgfx::GFXfont* const F_MONO = &fonts::FreeMonoBold9pt7b;
static const lgfx::GFXfont* const F_N40  = &fonts::DejaVu40;
static const lgfx::GFXfont* const F_N56  = &fonts::DejaVu56;
static const lgfx::GFXfont* const F_N72  = &fonts::DejaVu72;

// ============================================================================ texto UTF-8
// Las fuentes GFX solo traen ASCII 0x20-0x7E: las vocales acentuadas, la ñ y el símbolo °
// se componen dibujando la letra base y el diacrítico encima.
enum Mark : uint8_t { M_NONE, M_ACUTE, M_ACUTE_DOTLESS, M_TILDE, M_DIAER, M_DEGREE };

static uint16_t nextCp(const char*& p) {
  uint8_t c = (uint8_t)*p++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && ((uint8_t)*p & 0xC0) == 0x80) return ((c & 0x1F) << 6) | ((uint8_t)*p++ & 0x3F);
  if ((c & 0xF0) == 0xE0) {
    uint16_t cp = (c & 0x0F) << 12;
    if (*p) cp |= ((uint8_t)*p++ & 0x3F) << 6;
    if (*p) cp |= ((uint8_t)*p++ & 0x3F);
    return cp;
  }
  return '?';
}

static char mapCp(uint16_t cp, Mark& m) {
  m = M_NONE;
  switch (cp) {
    case 0xE1: m = M_ACUTE; return 'a';
    case 0xE9: m = M_ACUTE; return 'e';
    case 0xED: m = M_ACUTE_DOTLESS; return 'i';
    case 0xF3: m = M_ACUTE; return 'o';
    case 0xFA: m = M_ACUTE; return 'u';
    case 0xFC: m = M_DIAER; return 'u';
    case 0xF1: m = M_TILDE; return 'n';
    case 0xC1: m = M_ACUTE; return 'A';
    case 0xC9: m = M_ACUTE; return 'E';
    case 0xCD: m = M_ACUTE; return 'I';
    case 0xD3: m = M_ACUTE; return 'O';
    case 0xDA: m = M_ACUTE; return 'U';
    case 0xD1: m = M_TILDE; return 'N';
    case 0xB0: m = M_DEGREE; return ' ';
    case 0xBF: return '?';
    case 0xA1: return '!';
    case 0x2014: case 0x2013: return '-';
  }
  if (cp < 0x20 || cp > 0x7E) return '?';
  return (char)cp;
}

static const lgfx::GFXglyph* glyphOf(const lgfx::GFXfont* f, char c) {
  if ((uint8_t)c < f->first || (uint8_t)c > f->last) c = '?';
  return &f->glyph[(uint8_t)c - f->first];
}
static int capH(const lgfx::GFXfont* f) { return -glyphOf(f, 'H')->yOffset; }
static int xH(const lgfx::GFXfont* f)   { return -glyphOf(f, 'x')->yOffset; }
static int degR(const lgfx::GFXfont* f) { int r = capH(f) / 6; return r < 2 ? 2 : r; }

static int textW(const char* s, const lgfx::GFXfont* f) {
  int w = 0;
  while (*s) {
    Mark m;
    char c = mapCp(nextCp(s), m);
    if (m == M_DEGREE) w += degR(f) * 2 + 3;
    else w += glyphOf(f, c)->xAdvance;
  }
  return w;
}

enum Align : uint8_t { AL_LEFT, AL_CENTER, AL_RIGHT };

// Dibuja texto con la línea base en "base". bg se usa para borrar el punto de la "í".
static void text(lgfx::LovyanGFX* g, const char* s, int x, int base, const lgfx::GFXfont* f,
                 uint32_t fg, uint32_t bg, Align al = AL_LEFT) {
  if (al == AL_CENTER) x -= textW(s, f) / 2;
  else if (al == AL_RIGHT) x -= textW(s, f);
  g->setFont(f);
  g->setTextSize(1);
  g->setTextDatum(lgfx::textdatum_t::baseline_left);
  g->setTextColor(fg);
  const int ch = capH(f), xh = xH(f);
  const int th = f->yAdvance >= 26 ? 2 : 1;
  char buf[2] = {0, 0};
  while (*s) {
    Mark m;
    char c = mapCp(nextCp(s), m);
    if (m == M_DEGREE) {
      int r = degR(f);
      for (int t = 0; t < th; t++) g->drawCircle(x + r + 1, base - ch + r, r - t, fg);
      x += r * 2 + 3;
      continue;
    }
    const lgfx::GFXglyph* gl = glyphOf(f, c);
    buf[0] = c;
    g->drawString(buf, x, base);
    const bool upper = (c >= 'A' && c <= 'Z');
    const int top = base - (upper ? ch : xh);   // parte superior de la letra
    const int cx = x + gl->xOffset + gl->width / 2;
    if (m == M_ACUTE_DOTLESS) {
      int h = -gl->yOffset - xh - 1;
      if (h > 0) g->fillRect(x + gl->xOffset - 1, base + gl->yOffset - 1, gl->width + 2, h + 1, bg);
      m = M_ACUTE;
    }
    int ah = xh / 3; if (ah < 3) ah = 3;
    int gap = upper ? 1 : 2;
    if (m == M_ACUTE) {
      for (int t = 0; t < th + 1; t++)
        g->drawLine(cx - ah / 2 + t, top - gap, cx + ah / 2 + t, top - gap - ah, fg);
    } else if (m == M_TILDE) {
      int w = gl->width; if (w < 6) w = 6;
      int y0 = top - gap - 1;
      for (int t = 0; t < th; t++) {
        g->drawLine(cx - w / 2, y0 + t, cx - w / 6, y0 - 2 + t, fg);
        g->drawLine(cx - w / 6, y0 - 2 + t, cx + w / 6, y0 + t, fg);
        g->drawLine(cx + w / 6, y0 + t, cx + w / 2, y0 - 2 + t, fg);
      }
    } else if (m == M_DIAER) {
      int d = th + 1;
      g->fillRect(cx - gl->width / 3 - d / 2, top - gap - d, d, d, fg);
      g->fillRect(cx + gl->width / 3 - d / 2, top - gap - d, d, d, fg);
    }
    x += gl->xAdvance;
  }
}

// Texto centrado verticalmente en una caja [boxY, boxY+boxH)
static void textBox(lgfx::LovyanGFX* g, const char* s, int x, int boxY, int boxH,
                    const lgfx::GFXfont* f, uint32_t fg, uint32_t bg, Align al = AL_LEFT) {
  text(g, s, x, boxY + (boxH + capH(f)) / 2, f, fg, bg, al);
}

// ============================================================================ sprites
enum SpriteId { SP_TOP, SP_GAUGE, SP_GEAR, SP_LIGHTS, SP_TILE, SP_CANHDR, SP_ROW, SP_CHART,
                SP_SETROW, SP_INFO, SP_COUNT };
static LGFX_Sprite* s_spr[SP_COUNT] = {};

static LGFX_Sprite* spr(SpriteId id) { return s_spr[id]; }

static void makeSprite(SpriteId id, int w, int h) {
  auto* s = new LGFX_Sprite(&lcd);
  s->setColorDepth(16);
  s->setPsram(true);
  if (!s->createSprite(w, h)) Serial.printf("[UI] sin memoria para sprite %d (%dx%d)\n", id, w, h);
  s_spr[id] = s;
}

// ============================================================================ layout
enum Page : uint8_t { PG_DASH, PG_CAN, PG_CHARTS, PG_SETTINGS, PG_COUNT };
static const char* const kTabNames[PG_COUNT] = {"Tablero", "CAN", "Gráficas", "Ajustes"};

static constexpr int TOP_H = 56;
static constexpr int TAB_X0 = 168, TAB_W = 112;

// Tablero
static constexpr int G_SIZE = 280, G_SPD_X = 10, G_RPM_X = 510, G_Y = 64;
static constexpr int GEAR_X = 300, GEAR_Y = 64, GEAR_W = 200, GEAR_H = 150;
static constexpr int LIG_X = 300, LIG_Y = 222, LIG_W = 200, LIG_H = 122;
static constexpr int TILE_Y = 352, TILE_W = 252, TILE_H = 120;
static const int kTileX[3] = {10, 274, 538};

// CAN
static constexpr int CH_X = 10, CH_Y = 64, CH_W = 780, CH_H = 56;
static constexpr int BTN_PAUSE_X = 560, BTN_CLEAR_X = 676, BTN_Y = 8, BTN_W = 104, BTN_H = 40;
static constexpr int TBL_X = 10, TBL_Y = 128, ROW_H = 26;

// Gráficas
static constexpr int CHART_X = 10, CHART_W = 780, CHART_H = 130;
static const int kChartY[3] = {64, 202, 340};

// Ajustes
static constexpr int SET_X = 10, SET_Y = 64, SET_W = 460, SET_H = 96, SET_STEP = 102;
static constexpr int INFO_X = 480, INFO_Y = 64, INFO_W = 310, INFO_H = 404;
static const uint32_t kBitrates[4] = {125000, 250000, 500000, 1000000};
static const char* const kBitrateNames[4] = {"125k", "250k", "500k", "1M"};
static constexpr int SEG_X0 = 16, SEG_Y = 44, SEG_W = 100, SEG_H = 42, SEG_STEP = 108;
static constexpr int TGL_X = 366, TGL_Y = 30, TGL_W = 76, TGL_H = 38;
static constexpr int UNIT_X0 = 250, UNIT_W = 94, UNIT_Y = 28, UNIT_H = 40;

// ============================================================================ estado UI
struct Snapshot {
  VehicleData v;
  AppSettings set;
  int fps;
  BusState bus;
  bool paused;
  uint32_t framesRev, sampleRev;
};

static Snapshot snap() {
  Snapshot s;
  StateGuard g;
  s.v = g_state.v;
  s.set = g_state.settings;
  s.fps = g_state.fps;
  s.bus = g_state.bus;
  s.paused = g_state.paused;
  s.framesRev = g_state.framesRev;
  s.sampleRev = g_state.sampleRev;
  return s;
}

static Page s_page = PG_DASH;
static bool s_force = true;          // redibujar toda la página
static bool s_blApplied = true;

// cachés para dibujar solo lo que cambia
static struct {
  int page = -1; uint32_t up = 0xFFFFFFFF; int wifi = -9; int bus = -1; bool demo = false; bool paused = false;
} c_top;
static struct {
  int spd = -1, rpm = -1, gear = -1, lights = -1, temp = -999, fuel = -1, batt = -1; bool mph = false;
} c_dash;
static struct {
  int fps = -1, bus = -1; uint32_t br = 0; bool paused = false, demo = false;
  FrameEntry rows[FRAME_TABLE_SIZE]; int n = -1;
} c_can;
static struct { uint32_t rev = 0; bool mph = false; } c_chart;
static struct { AppSettings set; bool valid = false; uint32_t lastInfo = 0; } c_set;

static float speedDisp(float kmh, bool mph) { return mph ? kmh * 0.621371f : kmh; }
static const char* speedUnit(bool mph) { return mph ? "mph" : "km/h"; }

// ============================================================================ barra superior
static int wifiLevel(const NetInfo& n) {
  if (n.ap) return 10;
  if (!n.connected) return -1;
  if (n.rssi > -55) return 3;
  if (n.rssi > -67) return 2;
  if (n.rssi > -80) return 1;
  return 0;
}

static void drawWifiIcon(LGFX_Sprite* s, int cx, int by, int level) {
  // tres arcos + punto; nivel -1 = desconectado, 10 = AP
  if (level == 10) {
    s->fillRoundRect(cx - 18, by - 22, 36, 24, 6, C_ACCENT);
    text(s, "AP", cx, by - 4, F_SMB, C_BG, C_ACCENT, AL_CENTER);
    return;
  }
  for (int i = 0; i < 3; i++) {
    uint32_t col = (level > i) ? C_TEXT : C_LINE;
    int r = 7 + i * 7;
    s->fillArc(cx, by, r, r + 3, 225, 315, col);
  }
  s->fillCircle(cx, by - 1, 3, level >= 0 ? C_TEXT : C_LINE);
  if (level < 0) {
    s->drawLine(cx - 12, by - 22, cx + 12, by - 2, C_ERR);
    s->drawLine(cx - 11, by - 22, cx + 13, by - 2, C_ERR);
  }
}

static void drawTopBar(uint32_t up, int wifi, BusState bus, bool demo) {
  LGFX_Sprite* s = spr(SP_TOP);
  s->fillSprite(C_CARD);
  s->drawFastHLine(0, TOP_H - 1, LCD_W, C_LINE);
  text(s, "Moccia", 14, 36, F_MDB, C_TEXT, C_CARD);
  text(s, "CAN", 14 + textW("Moccia", F_MDB), 36, F_MDB, C_ACCENT, C_CARD);

  for (int i = 0; i < PG_COUNT; i++) {
    int x = TAB_X0 + i * TAB_W;
    bool act = (i == s_page);
    if (act) {
      s->fillRect(x + 4, 6, TAB_W - 8, TOP_H - 12, blend(C_CARD, C_ACCENT, 0.12f));
      s->fillRect(x + 10, TOP_H - 5, TAB_W - 20, 4, C_ACCENT);
    }
    textBox(s, kTabNames[i], x + TAB_W / 2, 0, TOP_H - 2, act ? F_SMB : F_SM, act ? C_TEXT : C_TEXT2,
            act ? blend(C_CARD, C_ACCENT, 0.12f) : C_CARD, AL_CENTER);
  }

  char buf[16];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)(up / 3600), (unsigned)((up / 60) % 60), (unsigned)(up % 60));
  text(s, buf, 664, 35, F_SM, C_TEXT2, C_CARD, AL_RIGHT);

  drawWifiIcon(s, 696, 40, wifi);

  uint32_t bc = bus == BusState::OK ? C_OK : bus == BusState::BUSOFF ? C_ERR : C_TEXT2;
  s->fillRoundRect(724, 14, 66, 28, 8, bc);
  text(s, demo ? "DEMO" : "CAN", 757, 34, F_SMB, C_BG, bc, AL_CENTER);
  s->pushSprite(0, 0);
}

static void updateTopBar(const Snapshot& sn, bool force) {
  uint32_t up = uptimeSeconds();
  int wifi = wifiLevel(netInfo());
  if (!force && c_top.page == s_page && c_top.up == up && c_top.wifi == wifi && c_top.bus == (int)sn.bus &&
      c_top.demo == sn.set.demo)
    return;
  c_top.page = s_page; c_top.up = up; c_top.wifi = wifi; c_top.bus = (int)sn.bus; c_top.demo = sn.set.demo;
  drawTopBar(up, wifi, sn.bus, sn.set.demo);
}

// ============================================================================ Tablero
static void drawGauge(int x, int y, float value, float maxv, float major, int minorPerMajor,
                      float labelDiv, float redFrom, const char* valTxt, const char* unit, const char* title) {
  LGFX_Sprite* s = spr(SP_GAUGE);
  const int cx = G_SIZE / 2, cy = G_SIZE / 2, R = 134, TH = 18;
  s->fillSprite(C_BG);
  s->fillCircle(cx, cy, R + 2, C_CARD);
  s->fillArc(cx, cy, R - TH, R - 4, 135, 405, C_LINE);
  if (redFrom > 0) s->fillArc(cx, cy, R - TH, R - 4, 135 + 270 * redFrom / maxv, 405, blend(C_LINE, C_ERR, 0.45f));
  float frac = value / maxv;
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  if (frac > 0.003f) {
    uint32_t col = (redFrom > 0 && value >= redFrom) ? C_ERR : C_ACCENT;
    s->fillArc(cx, cy, R - TH, R - 4, 135, 135 + 270 * frac, col);
  }
  // marcas
  int nMinor = (int)(maxv / major) * minorPerMajor;
  for (int i = 0; i <= nMinor; i++) {
    float a = (135 + 270.0f * i / nMinor) * DEG_TO_RAD;
    bool isMajor = (i % minorPerMajor) == 0;
    float r0 = R - TH - 4, r1 = r0 - (isMajor ? 12 : 6);
    float ca = cosf(a), sa = sinf(a);
    s->drawLine(cx + ca * r0, cy + sa * r0, cx + ca * r1, cy + sa * r1, isMajor ? C_TEXT : C_TEXT2);
    if (isMajor) {
      char b[8];
      snprintf(b, sizeof(b), "%d", (int)(i / minorPerMajor * major / labelDiv + 0.5f));
      float rl = r1 - 14;
      s->setFont(&fonts::Font2);
      s->setTextDatum(lgfx::textdatum_t::middle_center);
      s->setTextColor(C_TEXT2);
      s->drawString(b, cx + ca * rl, cy + sa * rl);
    }
  }
  text(s, valTxt, cx, cy + 22, F_N56, C_TEXT, C_CARD, AL_CENTER);
  text(s, unit, cx, cy + 54, F_MD, C_TEXT2, C_CARD, AL_CENTER);
  text(s, title, cx, cy + 112, F_SMB, C_TEXT2, C_CARD, AL_CENTER);
  s->pushSprite(x, y);
}

static void drawGear(int gear) {
  LGFX_Sprite* s = spr(SP_GEAR);
  s->fillSprite(C_BG);
  s->fillRoundRect(0, 0, GEAR_W, GEAR_H, 12, C_CARD);
  text(s, "Marcha", 14, 26, F_SMB, C_TEXT2, C_CARD);
  uint32_t col = gear == 1 ? C_WARN : gear == 0 ? C_TEXT2 : C_ACCENT;
  text(s, gearStr((uint8_t)gear), GEAR_W / 2, 128, F_N72, col, C_CARD, AL_CENTER);
  s->pushSprite(GEAR_X, GEAR_Y);
}

static void drawLights(uint8_t l) {
  LGFX_Sprite* s = spr(SP_LIGHTS);
  s->fillSprite(C_BG);
  s->fillRoundRect(0, 0, LIG_W, LIG_H, 12, C_CARD);
  text(s, "Luces", 14, 24, F_SMB, C_TEXT2, C_CARD);
  static const char* const names[6] = {"Cortas", "Largas", "Izq.", "Dcha.", "Freno", "Motor"};
  static const uint32_t colors[6] = {C_OK, C_ACCENT, C_OK, C_OK, C_ERR, C_WARN};
  for (int i = 0; i < 6; i++) {
    int col = i % 3, row = i / 3;
    int x = 8 + col * 63, y = 34 + row * 42;
    bool on = l & (1 << i);
    uint32_t bg = on ? colors[i] : C_LINE;
    s->fillRoundRect(x, y, 58, 36, 8, bg);
    // flechas para los intermitentes
    if (i == 2) s->fillTriangle(x + 6, y + 18, x + 13, y + 11, x + 13, y + 25, on ? C_BG : C_TEXT2);
    if (i == 3) s->fillTriangle(x + 52, y + 18, x + 45, y + 11, x + 45, y + 25, on ? C_BG : C_TEXT2);
    int tx = x + 29 + (i == 2 ? 5 : i == 3 ? -5 : 0);
    textBox(&*s, names[i], tx, y, 36, &fonts::FreeSans9pt7b, on ? C_BG : C_TEXT2, bg, AL_CENTER);
  }
  s->pushSprite(LIG_X, LIG_Y);
}

static void drawTile(int idx, const char* title, const char* val, const char* unit, float frac, uint32_t barCol) {
  LGFX_Sprite* s = spr(SP_TILE);
  s->fillSprite(C_BG);
  s->fillRoundRect(0, 0, TILE_W, TILE_H, 12, C_CARD);
  text(s, title, 14, 26, F_SMB, C_TEXT2, C_CARD);
  text(s, val, 14, 78, F_N40, C_TEXT, C_CARD);
  text(s, unit, 22 + textW(val, F_N40), 78, F_MD, C_TEXT2, C_CARD);
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  s->fillRoundRect(14, 94, TILE_W - 28, 12, 6, C_LINE);
  int w = (int)((TILE_W - 28) * frac);
  if (w > 0) s->fillRoundRect(14, 94, w < 12 ? 12 : w, 12, 6, barCol);
  s->pushSprite(kTileX[idx], TILE_Y);
}

static void updateDash(const Snapshot& sn, bool force) {
  const bool mph = sn.set.mph;
  if (force || mph != c_dash.mph) { c_dash.spd = -1; c_dash.mph = mph; }

  int spd = (int)(speedDisp(sn.v.speed, mph) + 0.5f);
  if (spd != c_dash.spd) {
    c_dash.spd = spd;
    char b[8];
    snprintf(b, sizeof(b), "%d", spd);
    drawGauge(G_SPD_X, G_Y, speedDisp(sn.v.speed, mph), mph ? 160 : 240, 20, 2, 1, 0, b,
              speedUnit(mph), "Velocidad");
  }
  int rpm = ((sn.v.rpm + 12) / 25) * 25;   // resolución de 25 rpm para no redibujar en exceso
  if (force || rpm != c_dash.rpm) {
    c_dash.rpm = rpm;
    char b[8];
    snprintf(b, sizeof(b), "%d", ((sn.v.rpm + 5) / 10) * 10);
    drawGauge(G_RPM_X, G_Y, rpm, 8000, 1000, 2, 1000, 6500, b, "rpm x1000", "Tacómetro");
  }
  if (force || sn.v.gear != c_dash.gear) { c_dash.gear = sn.v.gear; drawGear(sn.v.gear); }
  if (force || sn.v.lights != c_dash.lights) { c_dash.lights = sn.v.lights; drawLights(sn.v.lights); }
  if (force || sn.v.temp != c_dash.temp) {
    c_dash.temp = sn.v.temp;
    char b[8];
    snprintf(b, sizeof(b), "%d", sn.v.temp);
    uint32_t col = sn.v.temp >= 105 ? C_ERR : sn.v.temp >= 98 ? C_WARN : sn.v.temp < 60 ? C_ACCENT : C_OK;
    drawTile(0, "Temp. refrigerante", b, "°C", (sn.v.temp + 0.0f) / 130.0f, col);
  }
  if (force || sn.v.fuel != c_dash.fuel) {
    c_dash.fuel = sn.v.fuel;
    char b[8];
    snprintf(b, sizeof(b), "%d", sn.v.fuel);
    uint32_t col = sn.v.fuel <= 10 ? C_ERR : sn.v.fuel <= 20 ? C_WARN : C_OK;
    drawTile(1, "Combustible", b, "%", sn.v.fuel / 100.0f, col);
  }
  int battx10 = (int)(sn.v.batt * 10 + 0.5f);
  if (force || battx10 != c_dash.batt) {
    c_dash.batt = battx10;
    char b[8];
    snprintf(b, sizeof(b), "%.1f", battx10 / 10.0f);
    uint32_t col = (sn.v.batt < 11.8f || sn.v.batt > 15.0f) ? C_ERR : sn.v.batt < 12.4f ? C_WARN : C_OK;
    drawTile(2, "Batería", b, "V", (sn.v.batt - 10.0f) / 6.0f, col);
  }
}

// ============================================================================ CAN
static void drawButton(LGFX_Sprite* s, int x, int y, int w, int h, const char* label, uint32_t bg, uint32_t fg) {
  s->fillRoundRect(x, y, w, h, 8, bg);
  textBox(s, label, x + w / 2, y, h, F_SMB, fg, bg, AL_CENTER);
}

static void drawCanHeader(const Snapshot& sn) {
  LGFX_Sprite* s = spr(SP_CANHDR);
  s->fillSprite(C_BG);
  s->fillRoundRect(0, 0, CH_W, CH_H, 12, C_CARD);
  char b[32];
  text(s, "Tramas/s", 16, 22, F_SM, C_TEXT2, C_CARD);
  snprintf(b, sizeof(b), "%d", sn.fps);
  text(s, b, 16, 46, F_MDB, C_TEXT, C_CARD);

  text(s, "Bus", 130, 22, F_SM, C_TEXT2, C_CARD);
  const char* bt = sn.bus == BusState::OK ? "OK" : sn.bus == BusState::BUSOFF ? "BUS-OFF" : "SIN TRÁFICO";
  uint32_t bc = sn.bus == BusState::OK ? C_OK : sn.bus == BusState::BUSOFF ? C_ERR : C_WARN;
  text(s, bt, 130, 46, F_MDB, bc, C_CARD);

  text(s, "Bitrate", 300, 22, F_SM, C_TEXT2, C_CARD);
  if (sn.set.bitrate >= 1000000) snprintf(b, sizeof(b), "1 Mbit/s");
  else snprintf(b, sizeof(b), "%u kbit/s", (unsigned)(sn.set.bitrate / 1000));
  text(s, b, 300, 46, F_MDB, C_TEXT, C_CARD);

  if (sn.paused) text(s, "EN PAUSA", 440, 40, F_SMB, C_WARN, C_CARD);
  else if (sn.set.demo) text(s, "DEMO", 440, 40, F_SMB, C_ACCENT, C_CARD);

  drawButton(s, BTN_PAUSE_X, BTN_Y, BTN_W, BTN_H, sn.paused ? "Reanudar" : "Pausar",
             sn.paused ? C_WARN : C_ACCENT, C_BG);
  drawButton(s, BTN_CLEAR_X, BTN_Y, BTN_W, BTN_H, "Limpiar", C_LINE, C_TEXT);
  s->pushSprite(CH_X, CH_Y);
}

// columnas de la tabla
static constexpr int COL_ID = 14, COL_DLC = 130, COL_DATA = 190, COL_CNT_R = 640, COL_PER_R = 766;

static void drawTableHeader() {
  LGFX_Sprite* s = spr(SP_ROW);
  s->fillSprite(C_CARD);
  textBox(s, "ID", COL_ID, 0, ROW_H, F_SMB, C_TEXT2, C_CARD);
  textBox(s, "DLC", COL_DLC, 0, ROW_H, F_SMB, C_TEXT2, C_CARD);
  textBox(s, "Datos", COL_DATA, 0, ROW_H, F_SMB, C_TEXT2, C_CARD);
  textBox(s, "Contador", COL_CNT_R, 0, ROW_H, F_SMB, C_TEXT2, C_CARD, AL_RIGHT);
  textBox(s, "Periodo", COL_PER_R, 0, ROW_H, F_SMB, C_TEXT2, C_CARD, AL_RIGHT);
  s->pushSprite(TBL_X, TBL_Y);
}

static void drawTableRow(int i, const FrameEntry* f, bool emptyTable) {
  LGFX_Sprite* s = spr(SP_ROW);
  uint32_t bg = (i & 1) ? C_ROWALT : C_BG;
  s->fillSprite(bg);
  if (f) {
    char b[32];
    snprintf(b, sizeof(b), f->ext ? "0x%08X" : "0x%03X", (unsigned)f->id);
    textBox(s, b, COL_ID, 0, ROW_H, F_MONO, C_ACCENT, bg);
    snprintf(b, sizeof(b), "%u", f->dlc);
    textBox(s, b, COL_DLC + 10, 0, ROW_H, F_MONO, C_TEXT, bg);
    char* p = b;
    *p = 0;
    for (int k = 0; k < f->dlc && k < 8; k++) p += sprintf(p, k ? " %02X" : "%02X", f->data[k]);
    textBox(s, b, COL_DATA, 0, ROW_H, F_MONO, C_TEXT, bg);
    snprintf(b, sizeof(b), "%u", (unsigned)f->count);
    textBox(s, b, COL_CNT_R, 0, ROW_H, F_MONO, C_TEXT, bg, AL_RIGHT);
    if (f->count > 1) snprintf(b, sizeof(b), "%u ms", (unsigned)f->period);
    else snprintf(b, sizeof(b), "-");
    textBox(s, b, COL_PER_R, 0, ROW_H, F_MONO, C_TEXT2, bg, AL_RIGHT);
  } else if (i == 0 && emptyTable) {
    textBox(s, "Sin tramas recibidas", LCD_W / 2 - TBL_X, 0, ROW_H, F_SM, C_TEXT2, bg, AL_CENTER);
  }
  s->pushSprite(TBL_X, TBL_Y + ROW_H * (i + 1));
}

static bool sameFrame(const FrameEntry& a, const FrameEntry& b) {
  return a.used == b.used && a.id == b.id && a.ext == b.ext && a.dlc == b.dlc && a.count == b.count &&
         a.period == b.period && memcmp(a.data, b.data, 8) == 0;
}

static void updateCan(const Snapshot& sn, bool force) {
  if (force || sn.fps != c_can.fps || (int)sn.bus != c_can.bus || sn.set.bitrate != c_can.br ||
      sn.paused != c_can.paused || sn.set.demo != c_can.demo) {
    c_can.fps = sn.fps; c_can.bus = (int)sn.bus; c_can.br = sn.set.bitrate;
    c_can.paused = sn.paused; c_can.demo = sn.set.demo;
    drawCanHeader(sn);
  }
  if (force) {
    drawTableHeader();
    c_can.n = -1;
  }
  // la tabla se refresca a 4 Hz como máximo para que sea legible
  static uint32_t lastTbl = 0;
  uint32_t now = millis();
  if (!force && now - lastTbl < 250) return;
  lastTbl = now;

  FrameEntry rows[FRAME_TABLE_SIZE];
  int n = stateSnapshotFrames(rows);
  bool emptyChanged = (n == 0) != (c_can.n == 0);
  for (int i = 0; i < FRAME_TABLE_SIZE; i++) {
    const FrameEntry* f = i < n ? &rows[i] : nullptr;
    bool had = i < c_can.n;
    bool changed = force || c_can.n < 0 || (f && (!had || !sameFrame(*f, c_can.rows[i]))) || (!f && had) ||
                   (i == 0 && emptyChanged);
    if (changed) drawTableRow(i, f, n == 0);
    if (f) c_can.rows[i] = *f;
  }
  c_can.n = n;
}

// ============================================================================ Gráficas
static void drawChart(int idx, const char* title, const char* valTxt, const float* data, float vmax,
                      uint32_t col, int gridStep) {
  LGFX_Sprite* s = spr(SP_CHART);
  s->fillSprite(C_BG);
  s->fillRoundRect(0, 0, CHART_W, CHART_H, 12, C_CARD);
  text(s, title, 14, 24, F_SMB, C_TEXT, C_CARD);
  text(s, "últimos 120 s", 16 + textW(title, F_SMB) + 12, 24, F_SM, C_TEXT2, C_CARD);
  text(s, valTxt, CHART_W - 14, 26, F_MDB, col, C_CARD, AL_RIGHT);

  const int px = 56, py = 34, pw = CHART_W - px - 14, ph = CHART_H - py - 10;
  s->setFont(&fonts::Font0);
  s->setTextDatum(lgfx::textdatum_t::middle_right);
  s->setTextColor(C_TEXT2);
  for (float v = 0; v <= vmax + 0.01f; v += gridStep) {
    int y = py + ph - (int)(ph * v / vmax);
    s->drawFastHLine(px, y, pw, C_LINE);
    char b[8];
    snprintf(b, sizeof(b), "%d", (int)v);
    s->drawString(b, px - 6, y);
  }
  // área + línea
  uint32_t fill = blend(C_CARD, col, 0.22f);
  auto yOf = [&](float v) {
    if (v < 0) v = 0;
    if (v > vmax) v = vmax;
    return py + ph - (int)(ph * v / vmax);
  };
  for (int i = 0; i < HISTORY_LEN - 1; i++) {
    int x0 = px + i * (pw - 1) / (HISTORY_LEN - 1);
    int x1 = px + (i + 1) * (pw - 1) / (HISTORY_LEN - 1);
    int y0 = yOf(data[i]), y1 = yOf(data[i + 1]);
    for (int x = x0; x < x1; x++) {
      int y = y0 + (y1 - y0) * (x - x0) / (x1 - x0);
      s->drawFastVLine(x, y, py + ph - y, fill);
    }
  }
  for (int i = 0; i < HISTORY_LEN - 1; i++) {
    int x0 = px + i * (pw - 1) / (HISTORY_LEN - 1);
    int x1 = px + (i + 1) * (pw - 1) / (HISTORY_LEN - 1);
    int y0 = yOf(data[i]), y1 = yOf(data[i + 1]);
    s->drawLine(x0, y0, x1, y1, col);
    s->drawLine(x0, y0 - 1, x1, y1 - 1, col);
  }
  s->pushSprite(CHART_X, kChartY[idx]);
}

static void updateCharts(const Snapshot& sn, bool force) {
  if (!force && sn.sampleRev == c_chart.rev && sn.set.mph == c_chart.mph) return;
  c_chart.rev = sn.sampleRev;
  c_chart.mph = sn.set.mph;
  static float sp[HISTORY_LEN], rp[HISTORY_LEN], tp[HISTORY_LEN];
  static int16_t ri[HISTORY_LEN], ti[HISTORY_LEN];
  stateCopyHistory(sp, ri, ti);
  for (int i = 0; i < HISTORY_LEN; i++) {
    sp[i] = speedDisp(sp[i], sn.set.mph);
    rp[i] = ri[i];
    tp[i] = ti[i];
  }
  char b[24], t[32];
  const bool mph = sn.set.mph;
  snprintf(t, sizeof(t), "Velocidad (%s)", speedUnit(mph));
  snprintf(b, sizeof(b), "%.0f %s", sp[HISTORY_LEN - 1], speedUnit(mph));
  drawChart(0, t, b, sp, mph ? 160 : 240, C_ACCENT, mph ? 40 : 80);
  snprintf(b, sizeof(b), "%d rpm", ri[HISTORY_LEN - 1]);
  drawChart(1, "Revoluciones (rpm)", b, rp, 8000, C_OK, 2000);
  snprintf(b, sizeof(b), "%d °C", ti[HISTORY_LEN - 1]);
  drawChart(2, "Temperatura refrigerante (°C)", b, tp, 120, C_WARN, 40);
}

// ============================================================================ Ajustes
static void drawToggle(LGFX_Sprite* s, int x, int y, bool on) {
  uint32_t bg = on ? C_OK : C_LINE;
  s->fillRoundRect(x, y, TGL_W, TGL_H, TGL_H / 2, bg);
  int r = TGL_H / 2 - 4;
  int kx = on ? x + TGL_W - TGL_H / 2 : x + TGL_H / 2;
  s->fillCircle(kx, y + TGL_H / 2, r, C_TEXT);
}

static void drawSeg(LGFX_Sprite* s, int x, int y, int w, int h, const char* label, bool sel) {
  uint32_t bg = sel ? C_ACCENT : C_LINE;
  s->fillRoundRect(x, y, w, h, 8, bg);
  textBox(s, label, x + w / 2, y, h, F_SMB, sel ? C_BG : C_TEXT, bg, AL_CENTER);
}

static void drawSettingRow(int row, const AppSettings& st) {
  LGFX_Sprite* s = spr(SP_SETROW);
  s->fillSprite(C_BG);
  s->fillRoundRect(0, 0, SET_W, SET_H, 12, C_CARD);
  switch (row) {
    case 0:
      text(s, "Bitrate CAN", 16, 32, F_MDB, C_TEXT, C_CARD);
      for (int i = 0; i < 4; i++)
        drawSeg(s, SEG_X0 + i * SEG_STEP, SEG_Y, SEG_W, SEG_H, kBitrateNames[i], st.bitrate == kBitrates[i]);
      break;
    case 1:
      text(s, "Modo demo", 16, 40, F_MDB, C_TEXT, C_CARD);
      text(s, "Simula datos de conducción", 16, 70, F_SM, C_TEXT2, C_CARD);
      drawToggle(s, TGL_X, TGL_Y, st.demo);
      break;
    case 2:
      text(s, "Pantalla", 16, 40, F_MDB, C_TEXT, C_CARD);
      text(s, "Retroiluminación on/off", 16, 70, F_SM, C_TEXT2, C_CARD);
      drawToggle(s, TGL_X, TGL_Y, st.backlight);
      break;
    case 3:
      text(s, "Unidades", 16, 40, F_MDB, C_TEXT, C_CARD);
      text(s, "Velocidad", 16, 70, F_SM, C_TEXT2, C_CARD);
      drawSeg(s, UNIT_X0, UNIT_Y, UNIT_W, UNIT_H, "km/h", !st.mph);
      drawSeg(s, UNIT_X0 + UNIT_W + 6, UNIT_Y, UNIT_W, UNIT_H, "mph", st.mph);
      break;
  }
  s->pushSprite(SET_X, SET_Y + row * SET_STEP);
}

static void fmtBytes(char* b, size_t n, uint32_t v) {
  if (v >= 1024 * 1024) snprintf(b, n, "%.2f MB", v / 1048576.0f);
  else snprintf(b, n, "%u KB", (unsigned)(v / 1024));
}

static void drawInfo() {
  LGFX_Sprite* s = spr(SP_INFO);
  s->fillSprite(C_BG);
  s->fillRoundRect(0, 0, INFO_W, INFO_H, 12, C_CARD);
  text(s, "Sistema", 16, 34, F_MDB, C_TEXT, C_CARD);
  NetInfo n = netInfo();
  char v[40];
  int y = 74;
  auto line = [&](const char* k, const char* val, uint32_t col = C_TEXT) {
    text(s, k, 16, y, F_SM, C_TEXT2, C_CARD);
    text(s, val, INFO_W - 16, y, F_SMB, col, C_CARD, AL_RIGHT);
    s->drawFastHLine(16, y + 11, INFO_W - 32, C_LINE);
    y += 33;
  };
  line("IP", n.ip[0] ? n.ip : "-");
  line("SSID", n.ssid[0] ? n.ssid : "-");
  if (n.ap) snprintf(v, sizeof(v), "-");
  else snprintf(v, sizeof(v), "%d dBm", n.rssi);
  line("RSSI", v);
  line("Modo WiFi", n.ap ? "AP" : "STA", n.ap ? C_WARN : C_OK);
  fmtBytes(v, sizeof(v), ESP.getFreeHeap());
  line("Heap libre", v);
  fmtBytes(v, sizeof(v), ESP.getFreePsram());
  line("PSRAM libre", v);
  uint32_t up = uptimeSeconds();
  if (up >= 86400)
    snprintf(v, sizeof(v), "%ud %02u:%02u:%02u", (unsigned)(up / 86400), (unsigned)((up / 3600) % 24),
             (unsigned)((up / 60) % 60), (unsigned)(up % 60));
  else
    snprintf(v, sizeof(v), "%02u:%02u:%02u", (unsigned)(up / 3600), (unsigned)((up / 60) % 60), (unsigned)(up % 60));
  line("Uptime", v);
  line("Firmware", FW_VERSION);
  line("Driver CAN", canDriverOk() ? "OK" : "Error", canDriverOk() ? C_OK : C_ERR);
  line("mDNS", "moccia.local");
  s->pushSprite(INFO_X, INFO_Y);
}

static void updateSettings(const Snapshot& sn, bool force) {
  const AppSettings& st = sn.set;
  const AppSettings& o = c_set.set;
  bool all = force || !c_set.valid;
  if (all || st.bitrate != o.bitrate) drawSettingRow(0, st);
  if (all || st.demo != o.demo) drawSettingRow(1, st);
  if (all || st.backlight != o.backlight) drawSettingRow(2, st);
  if (all || st.mph != o.mph) drawSettingRow(3, st);
  c_set.set = st;
  c_set.valid = true;
  uint32_t now = millis();
  if (all || now - c_set.lastInfo >= 1000) {
    c_set.lastInfo = now;
    drawInfo();
  }
}

// ============================================================================ táctil
static bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void setPage(Page p) {
  if (p == s_page) return;
  s_page = p;
  s_force = true;
}

static void onTouch(int x, int y) {
  // barra superior: pestañas
  if (y < TOP_H) {
    if (x >= TAB_X0 && x < TAB_X0 + TAB_W * PG_COUNT) setPage((Page)((x - TAB_X0) / TAB_W));
    return;
  }
  AppSettings st = settingsGet();
  switch (s_page) {
    case PG_CAN: {
      int lx = x - CH_X, ly = y - CH_Y;
      if (inRect(lx, ly, BTN_PAUSE_X, BTN_Y, BTN_W, BTN_H)) {
        bool paused;
        { StateGuard g; paused = g_state.paused; }
        stateSetPaused(!paused);
      } else if (inRect(lx, ly, BTN_CLEAR_X, BTN_Y, BTN_W, BTN_H)) {
        stateClearFrames();
      }
      break;
    }
    case PG_SETTINGS: {
      if (x < SET_X || x >= SET_X + SET_W || y < SET_Y) break;
      int row = (y - SET_Y) / SET_STEP;
      int ly = (y - SET_Y) - row * SET_STEP, lx = x - SET_X;
      if (row > 3 || ly >= SET_H) break;
      if (row == 0) {
        for (int i = 0; i < 4; i++)
          if (inRect(lx, ly, SEG_X0 + i * SEG_STEP, SEG_Y - 4, SEG_W, SEG_H + 8)) settingsSetBitrate(kBitrates[i]);
      } else if (row == 1) {
        settingsSetDemo(!st.demo);
      } else if (row == 2) {
        settingsSetBacklight(!st.backlight);
      } else if (row == 3) {
        if (inRect(lx, ly, UNIT_X0, 0, UNIT_W + 3, SET_H)) settingsSetMph(false);
        else if (inRect(lx, ly, UNIT_X0 + UNIT_W + 3, 0, UNIT_W + 3, SET_H)) settingsSetMph(true);
      }
      break;
    }
    default:
      break;
  }
}

static void pollTouch() {
  static bool wasDown = false;
  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  if (now - lastPoll < 15) return;
  lastPoll = now;
  int32_t x, y;
  bool down = lcd.getTouch(&x, &y) > 0;
  if (down && !wasDown) {
    bool bl;
    { StateGuard g; bl = g_state.settings.backlight; }
    if (!bl) settingsSetBacklight(true);   // con la pantalla apagada, un toque la enciende
    else onTouch(x, y);
  }
  wasDown = down;
}

// ============================================================================ API
void uiBegin() {
  lcd.init();
  ch422gHandoffToLgfx();   // a partir de aquí el CH422G se maneja por lgfx::i2c desde esta tarea
  lcd.setRotation(0);
  lcd.fillScreen(C_BG);

  makeSprite(SP_TOP, LCD_W, TOP_H);
  makeSprite(SP_GAUGE, G_SIZE, G_SIZE);
  makeSprite(SP_GEAR, GEAR_W, GEAR_H);
  makeSprite(SP_LIGHTS, LIG_W, LIG_H);
  makeSprite(SP_TILE, TILE_W, TILE_H);
  makeSprite(SP_CANHDR, CH_W, CH_H);
  makeSprite(SP_ROW, CH_W, ROW_H);
  makeSprite(SP_CHART, CHART_W, CHART_H);
  makeSprite(SP_SETROW, SET_W, SET_H);
  makeSprite(SP_INFO, INFO_W, INFO_H);

  // pantalla de arranque
  text(&lcd, "MocciaCAN", LCD_W / 2, 230, F_LGB, C_ACCENT, C_BG, AL_CENTER);
  text(&lcd, "Iniciando...", LCD_W / 2, 270, F_SM, C_TEXT2, C_BG, AL_CENTER);
  s_blApplied = settingsGet().backlight;
}

void uiLoop() {
  pollTouch();

  static uint32_t lastFrame = 0;
  uint32_t now = millis();
  if (!s_force && now - lastFrame < 40) return;   // ~25 fps máximo
  lastFrame = now;

  Snapshot sn = snap();

  // retroiluminación (el CH422G solo se toca desde esta tarea)
  if (sn.set.backlight != s_blApplied) {
    if (ch422gSetBacklight(sn.set.backlight)) s_blApplied = sn.set.backlight;
  }

  bool force = s_force;
  if (force) {
    lcd.fillRect(0, TOP_H, LCD_W, LCD_H - TOP_H, C_BG);
    c_set.valid = false;
  }
  s_force = false;

  updateTopBar(sn, force);
  switch (s_page) {
    case PG_DASH:     updateDash(sn, force); break;
    case PG_CAN:      updateCan(sn, force); break;
    case PG_CHARTS:   updateCharts(sn, force); break;
    case PG_SETTINGS: updateSettings(sn, force); break;
    default: break;
  }
}
