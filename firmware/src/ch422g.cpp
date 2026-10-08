#include "ch422g.h"
#include <Wire.h>
#include <LovyanGFX.hpp>
#include "config.h"

static uint8_t s_out = 0;          // copia del registro de salidas
static bool s_useLgfx = false;

static bool writeWire(uint8_t addr, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool writeReg(uint8_t addr, uint8_t val) {
  if (s_useLgfx) {
    return lgfx::i2c::transactionWrite(I2C_PORT, addr, &val, 1, I2C_FREQ).has_value();
  }
  return writeWire(addr, val);
}

static inline void setBit(int bit, bool on) {
  if (on) s_out |= (1u << bit);
  else    s_out &= ~(1u << bit);
}

bool ch422gBegin(bool backlightOn) {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);

  bool ok = writeWire(CH422G_ADDR_MODE, 0x01);   // IO0-7 en modo salida

  // Estado inicial: táctil en reset, LCD fuera de reset, SD deseleccionada, USB_SEL=1 (CAN)
  s_out = 0;
  setBit(EXIO_TP_RST, false);
  setBit(EXIO_LCD_RST, false);
  setBit(EXIO_SD_CS, true);
  setBit(EXIO_USB_SEL, true);
  setBit(EXIO_BL, false);
  ok &= writeWire(CH422G_ADDR_OUT, s_out);

  // Secuencia de reset del GT911: INT a nivel bajo durante el flanco de subida de RST
  // selecciona la dirección 0x5D.
  pinMode(PIN_TP_INT, OUTPUT);
  digitalWrite(PIN_TP_INT, LOW);
  delay(20);
  setBit(EXIO_LCD_RST, true);
  ok &= writeWire(CH422G_ADDR_OUT, s_out);
  delay(10);
  setBit(EXIO_TP_RST, true);
  ok &= writeWire(CH422G_ADDR_OUT, s_out);
  delay(60);
  pinMode(PIN_TP_INT, INPUT);
  delay(50);

  // Retroiluminación
  setBit(EXIO_BL, backlightOn);
  ok &= writeWire(CH422G_ADDR_OUT, s_out);

  // Liberar Wire: LovyanGFX reconfigura el puerto I2C0 en lcd.init()
  Wire.end();
  return ok;
}

void ch422gHandoffToLgfx() { s_useLgfx = true; }

bool ch422gSetBacklight(bool on) {
  setBit(EXIO_BL, on);
  return writeReg(CH422G_ADDR_OUT, s_out);
}
