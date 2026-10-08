// Configuración LovyanGFX para Waveshare ESP32-S3-Touch-LCD-7 (RGB 800x480 + GT911).
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include "app_config.h"

class LGFX : public lgfx::LGFX_Device {
 public:
  lgfx::Bus_RGB     _bus;
  lgfx::Panel_RGB   _panel;
  lgfx::Touch_GT911 _touch;

  LGFX() {
    {
      auto cfg = _panel.config();
      cfg.memory_width  = LCD_W;
      cfg.memory_height = LCD_H;
      cfg.panel_width   = LCD_W;
      cfg.panel_height  = LCD_H;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.use_psram = 1;   // framebuffer en PSRAM
      _panel.config_detail(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;
      // d0..d4 = B0..B4, d5..d10 = G0..G5, d11..d15 = R0..R4
      cfg.pin_d0  = PIN_LCD_B0;
      cfg.pin_d1  = PIN_LCD_B1;
      cfg.pin_d2  = PIN_LCD_B2;
      cfg.pin_d3  = PIN_LCD_B3;
      cfg.pin_d4  = PIN_LCD_B4;
      cfg.pin_d5  = PIN_LCD_G0;
      cfg.pin_d6  = PIN_LCD_G1;
      cfg.pin_d7  = PIN_LCD_G2;
      cfg.pin_d8  = PIN_LCD_G3;
      cfg.pin_d9  = PIN_LCD_G4;
      cfg.pin_d10 = PIN_LCD_G5;
      cfg.pin_d11 = PIN_LCD_R0;
      cfg.pin_d12 = PIN_LCD_R1;
      cfg.pin_d13 = PIN_LCD_R2;
      cfg.pin_d14 = PIN_LCD_R3;
      cfg.pin_d15 = PIN_LCD_R4;

      cfg.pin_henable = PIN_LCD_DE;
      cfg.pin_vsync   = PIN_LCD_VSYNC;
      cfg.pin_hsync   = PIN_LCD_HSYNC;
      cfg.pin_pclk    = PIN_LCD_PCLK;
      cfg.freq_write  = LCD_PCLK_HZ;

      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = LCD_HSYNC_FRONT;
      cfg.hsync_pulse_width = LCD_HSYNC_PULSE;
      cfg.hsync_back_porch  = LCD_HSYNC_BACK;
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = LCD_VSYNC_FRONT;
      cfg.vsync_pulse_width = LCD_VSYNC_PULSE;
      cfg.vsync_back_porch  = LCD_VSYNC_BACK;
      cfg.pclk_active_neg   = 1;   // datos en flanco de bajada
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;
      _bus.config(cfg);
    }
    _panel.setBus(&_bus);

    {
      auto cfg = _touch.config();
      cfg.x_min = 0;
      cfg.x_max = LCD_W - 1;
      cfg.y_min = 0;
      cfg.y_max = LCD_H - 1;
      cfg.pin_int  = PIN_TP_INT;
      cfg.pin_rst  = -1;          // el reset lo hace el CH422G (EXIO1)
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = I2C_PORT;
      cfg.pin_sda  = PIN_I2C_SDA;
      cfg.pin_scl  = PIN_I2C_SCL;
      cfg.freq     = I2C_FREQ;
      cfg.i2c_addr = GT911_ADDR;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
