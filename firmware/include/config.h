// MocciaCAN — constantes de hardware y aplicación (ver docs/SPEC.md)
#pragma once
#include <stdint.h>

#define FW_VERSION "1.0.0"
#define FW_NAME    "MocciaCAN"

// ---------------- Pantalla RGB 800x480 (ST7262) ----------------
#define LCD_W 800
#define LCD_H 480

#define PIN_LCD_DE    5
#define PIN_LCD_VSYNC 3
#define PIN_LCD_HSYNC 46
#define PIN_LCD_PCLK  7
// R0..R4
#define PIN_LCD_R0 1
#define PIN_LCD_R1 2
#define PIN_LCD_R2 42
#define PIN_LCD_R3 41
#define PIN_LCD_R4 40
// G0..G5
#define PIN_LCD_G0 39
#define PIN_LCD_G1 0
#define PIN_LCD_G2 45
#define PIN_LCD_G3 48
#define PIN_LCD_G4 47
#define PIN_LCD_G5 21
// B0..B4
#define PIN_LCD_B0 14
#define PIN_LCD_B1 38
#define PIN_LCD_B2 18
#define PIN_LCD_B3 17
#define PIN_LCD_B4 10

#define LCD_PCLK_HZ        16000000
#define LCD_HSYNC_PULSE    4
#define LCD_HSYNC_BACK     8
#define LCD_HSYNC_FRONT    8
#define LCD_VSYNC_PULSE    4
#define LCD_VSYNC_BACK     8
#define LCD_VSYNC_FRONT    8

// ---------------- I2C compartido (GT911 + CH422G) ----------------
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9
#define I2C_PORT    0
#define I2C_FREQ    400000
#define PIN_TP_INT  4
#define GT911_ADDR  0x5D   // alternativa 0x14 (LovyanGFX prueba ambas)

// ---------------- CH422G ----------------
#define CH422G_ADDR_MODE 0x24   // escribir 0x01 => IO0-7 como salidas
#define CH422G_ADDR_OUT  0x38   // registro de salidas IO0-7
#define EXIO_TP_RST   1
#define EXIO_BL       2
#define EXIO_LCD_RST  3
#define EXIO_SD_CS    4
#define EXIO_USB_SEL  5         // 1 = CAN (GPIO19/20 al TJA1051)

// ---------------- CAN / TWAI ----------------
#define PIN_CAN_TX 20
#define PIN_CAN_RX 19

// ---------------- Aplicación ----------------
#define FRAME_TABLE_SIZE 12
#define HISTORY_LEN      120

// Colores del estilo común (RGB888)
#define COL_BG        0x0f1115
#define COL_CARD      0x1a1d24
#define COL_ACCENT    0x00c2ff
#define COL_OK        0x2ecc71
#define COL_WARN      0xf1c40f
#define COL_ERR       0xe74c3c
#define COL_TEXT      0xe6e6e6
#define COL_TEXT2     0x8a8f98
#define COL_LINE      0x2a2e37
