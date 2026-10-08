#include "app_settings.h"
#include <Preferences.h>
#include "can_bus.h"

static const char* NS = "moccia";

bool settingsValidBitrate(uint32_t br) {
  return br == 125000 || br == 250000 || br == 500000 || br == 1000000;
}

void settingsLoad() {
  Preferences p;
  AppSettings s;
  if (p.begin(NS, true)) {
    s.bitrate   = p.getUInt("bitrate", 500000);
    s.demo      = p.getBool("demo", true);
    s.backlight = p.getBool("bl", true);
    s.mph       = p.getBool("mph", false);
    p.end();
  }
  if (!settingsValidBitrate(s.bitrate)) s.bitrate = 500000;
  StateGuard g;
  g_state.settings = s;
  g_state.settingsRev++;
}

AppSettings settingsGet() {
  StateGuard g;
  return g_state.settings;
}

static void persist(const AppSettings& s) {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.putUInt("bitrate", s.bitrate);
  p.putBool("demo", s.demo);
  p.putBool("bl", s.backlight);
  p.putBool("mph", s.mph);
  p.end();
}

// Aplica una modificación; si algo cambió, incrementa la revisión y persiste.
template <typename F>
static bool modify(F fn) {
  AppSettings copy;
  bool changed;
  {
    StateGuard g;
    AppSettings before = g_state.settings;
    fn(g_state.settings);
    const AppSettings& a = g_state.settings;
    changed = before.bitrate != a.bitrate || before.demo != a.demo ||
              before.backlight != a.backlight || before.mph != a.mph;
    // Aunque no cambie, re-difundimos para que el cliente que pidió el cambio se resincronice.
    g_state.settingsRev++;
    copy = a;
  }
  if (changed) persist(copy);
  return changed;
}

bool settingsSetBitrate(uint32_t br) {
  if (!settingsValidBitrate(br)) return false;
  if (modify([br](AppSettings& s) { s.bitrate = br; })) canRequestBitrate(br);
  return true;
}

void settingsSetDemo(bool on)      { modify([on](AppSettings& s) { s.demo = on; }); }
void settingsSetBacklight(bool on) { modify([on](AppSettings& s) { s.backlight = on; }); }
void settingsSetMph(bool mph)      { modify([mph](AppSettings& s) { s.mph = mph; }); }
