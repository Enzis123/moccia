// Expansor de E/S CH422G (I2C). EXIO1=TP_RST, EXIO2=retroiluminación, EXIO3=LCD_RST,
// EXIO4=SD_CS, EXIO5=USB_SEL (1 = CAN).
//
// Acceso al bus I2C:
//  - Antes de lcd.init() se usa Wire (ch422gBegin()).
//  - Después, LovyanGFX toma el puerto I2C0 para el GT911 con su propio driver; para no
//    pelear por el periférico, las escrituras posteriores (ch422gSetBacklight) usan
//    lgfx::i2c sobre el mismo puerto y SOLO deben llamarse desde la tarea de UI
//    (la misma que lee el táctil), de modo que nunca hay dos transacciones simultáneas.
#pragma once
#include <Arduino.h>

// Inicializa el CH422G por Wire: modo salida, reset de táctil y LCD, retroiluminación,
// USB_SEL=1 (CAN). Deja Wire liberado para que LovyanGFX use el puerto.
bool ch422gBegin(bool backlightOn);

// Cambia a acceso vía lgfx::i2c (llamar tras lcd.init()).
void ch422gHandoffToLgfx();

// Enciende/apaga la retroiluminación (desde la tarea de UI).
bool ch422gSetBacklight(bool on);
