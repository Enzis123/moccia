# MocciaCAN — Especificación compartida (firmware + web)

Dispositivo: **Waveshare ESP32-S3-Touch-LCD-7** (ESP32-S3-N16R8, 16 MB flash, 8 MB PSRAM OPI,
pantalla RGB 800x480 ST7262, táctil capacitivo GT911, expansor IO CH422G, transceptor CAN TJA1051).

Aplicación elegida: **Panel de vehículo / monitor de bus CAN**. El firmware lee el bus CAN (TWAI)
—o simula datos cuando no hay tráfico o el modo demo está activo— y los muestra en la pantalla táctil.
El mismo ESP32 sirve la app web (desde LittleFS) y la mantiene sincronizada por WebSocket.
Ambas interfaces muestran **las mismas 4 páginas con los mismos datos** y cualquier acción hecha en
una se refleja en la otra.

## Estructura del repositorio
```
firmware/                PlatformIO (framework arduino)
  platformio.ini
  src/*.cpp, include/*.h
  data/                  <- app web (se sube con `pio run -t uploadfs` a LittleFS)
    index.html, app.css, app.js
web/                     NO duplicar: la app web vive solo en firmware/data/
tools/mock_server.py     servidor de pruebas en PC que imita al ESP32 (HTTP + WebSocket)
docs/SPEC.md             este documento
README.md                instrucciones en español
```

## Páginas (idénticas en pantalla y web, mismo orden y nombres)
1. **Tablero** – velocímetro (km/h, 0-240), tacómetro (rpm, 0-8000), temperatura refrigerante (°C),
   nivel de combustible (%), voltaje batería (V), marcha (P/R/N/D o 1-6), indicadores de luces.
2. **CAN** – monitor de tramas: tabla de las últimas 12 IDs distintas (ID hex, DLC, datos hex,
   contador, periodo ms), tramas/s, estado del bus (OK / BUS-OFF / SIN TRÁFICO), bitrate.
   Botón "Pausar/Reanudar" y botón "Limpiar".
3. **Gráficas** – historial de los últimos 120 s (1 muestra/s) de velocidad y rpm (y temperatura).
4. **Ajustes** – bitrate CAN (125k/250k/500k/1M), modo demo on/off, brillo (en este HW solo
   on/off vía CH422G; mostrar como interruptor), unidades (km/h ↔ mph), info del sistema
   (IP, SSID, RSSI, heap libre, PSRAM libre, uptime, versión firmware).

Barra superior común: título "MocciaCAN", pestañas de las 4 páginas, hora de uptime, iconos de
WiFi y CAN. La página activa NO se sincroniza entre clientes (cada pantalla navega libremente),
pero todo lo demás (datos, ajustes, pausa del monitor) sí.

## Mapeo CAN (decodificación, IDs estándar 11 bits) — usado también por el simulador
| ID    | Bytes | Señal |
|-------|-------|-------|
| 0x100 | 0-1   | rpm (uint16 BE, rpm)                   |
| 0x100 | 2     | marcha (0=P,1=R,2=N,3=D,4..9 = 1..6)   |
| 0x101 | 0-1   | velocidad (uint16 BE, 0.01 km/h)       |
| 0x102 | 0     | temp refrigerante (uint8, °C + 40 offset) |
| 0x102 | 1     | combustible (uint8, %)                 |
| 0x102 | 2-3   | batería (uint16 BE, mV)                |
| 0x103 | 0     | luces bitmask: b0 cortas, b1 largas, b2 intermitente izq, b3 dcho, b4 freno de mano, b5 check engine |
Además se aceptan respuestas OBD-II (0x7E8, modo 01 PIDs 0x0C rpm, 0x0D velocidad, 0x05 temp).

## Protocolo WebSocket `ws://<ip>/ws` (JSON texto)
Servidor → cliente:
- `{"t":"state", "d":{...}}` cada 100 ms (10 Hz):
  `{"rpm":int,"speed":float(km/h),"temp":int,"fuel":int,"batt":float(V),"gear":"P|R|N|D|1..6",
    "lights":int,"fps":int(tramas/s),"bus":"OK|BUSOFF|IDLE","demo":bool,"paused":bool,
    "uptime":int(s)}`
- `{"t":"frames","d":[{"id":"0x100","dlc":8,"data":"0B B8 03 ...","count":int,"period":int}, ...]}` cada 500 ms
- `{"t":"history","d":{"speed":[...120],"rpm":[...120],"temp":[...120]}}` al conectar y luego
  `{"t":"sample","d":{"speed":f,"rpm":i,"temp":i}}` cada 1 s
- `{"t":"settings","d":{"bitrate":500000,"demo":true,"backlight":true,"units":"kmh"}}` al conectar y en cada cambio
- `{"t":"sys","d":{"ip":"","ssid":"","rssi":int,"heap":int,"psram":int,"ver":"1.0.0","mode":"AP|STA"}}` al conectar y cada 5 s

Cliente → servidor:
- `{"t":"set","k":"bitrate"|"demo"|"backlight"|"units","v":valor}`
- `{"t":"cmd","c":"pause"|"resume"|"clear"}`
El servidor aplica, persiste ajustes en NVS (Preferences) y re-difunde `settings`/`state` a todos.
Los cambios hechos con la pantalla táctil también se difunden.

HTTP: `GET /` → index.html (LittleFS), `GET /api/state` → mismo JSON que `state.d`,
`GET /api/settings`, `POST /api/settings` (JSON parcial). 

## Red
- Intenta STA con credenciales de `firmware/include/secrets.h` (plantilla `secrets.example.h`,
  `secrets.h` en .gitignore; si no existe, compila igual con credenciales vacías).
- Si falla en 10 s → AP `MocciaCAN` / clave `moccia1234`, IP 192.168.4.1. mDNS `moccia.local`.

## Pines (Waveshare ESP32-S3-Touch-LCD-7)
- RGB: DE 5, VSYNC 3, HSYNC 46, PCLK 7; R: 1,2,42,41,40; G: 39,0,45,48,47,21; B: 14,38,18,17,10;
  hsync pulse/back/front 4/8/8, vsync 4/8/8, PCLK 16 MHz, pclk activo flanco bajo.
- I2C: SDA 8, SCL 9 (compartido GT911 0x5D/0x14 y CH422G). Touch INT GPIO4.
- CH422G (escribir 0x01 a dir 0x24 para modo salida, salidas en 0x38):
  EXIO1 = TP_RST, EXIO2 = backlight, EXIO3 = LCD_RST, EXIO4 = SD_CS, EXIO5 = USB_SEL (1 = CAN).
- CAN (TWAI): TX GPIO20, RX GPIO19 (requiere EXIO5 = 1 → USB nativo deshabilitado; usar UART0 para monitor).

## Estilo visual (común)
Fondo #0f1115, tarjetas #1a1d24, acento #00c2ff, OK #2ecc71, aviso #f1c40f, error #e74c3c,
texto #e6e6e6 / secundario #8a8f98. Fuente sans. Interfaz en español.
