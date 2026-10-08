# MocciaCAN

Panel de vehículo y monitor de bus CAN para el kit **Waveshare ESP32-S3-Touch-LCD-7**
(pantalla táctil capacitiva de 7", 800x480, ESP32-S3 con CAN).

La aplicación se compone de dos interfaces que muestran **lo mismo**:

- **App en la pantalla**: firmware grabado en la memoria del ESP32-S3, manejado con el táctil.
- **App web**: la sirve el propio ESP32 desde su memoria (LittleFS). Funciona en PC, tablet y móvil.

Las dos están sincronizadas por WebSocket. Cualquier ajuste o acción hecha en una se ve al
instante en la otra. Lo único que no se sincroniza es la página abierta: cada pantalla navega por
su cuenta.

> Especificación técnica completa (protocolo, mapeo CAN, pines): [`docs/SPEC.md`](docs/SPEC.md)

---

## Páginas

| # | Página | Contenido |
|---|--------|-----------|
| 1 | **Tablero** | Velocímetro (0-240 km/h), tacómetro (0-8000 rpm), temperatura del refrigerante, combustible, batería, marcha (P/R/N/D, 1-6) y testigos de luces |
| 2 | **CAN** | Tabla con las últimas 12 IDs recibidas (ID, DLC, datos, contador, periodo), tramas/s, estado del bus y bitrate. Botones **Pausar/Reanudar** y **Limpiar** |
| 3 | **Gráficas** | Historial de los últimos 120 s de velocidad, rpm y temperatura |
| 4 | **Ajustes** | Bitrate CAN (125k/250k/500k/1M), modo demo, retroiluminación, unidades (km/h ↔ mph) e información del sistema (IP, SSID, RSSI, memoria, uptime, versión) |

Barra superior común: título, pestañas, tiempo encendido e iconos de WiFi y CAN.

**Modo demo:** si está activo, o si no hay tráfico en el bus, el ESP32 simula una conducción
realista. Así se puede probar la app sin conectar el kit a un vehículo.

---

## Hardware

| Componente | Detalle |
|------------|---------|
| MCU | ESP32-S3-N16R8 (16 MB flash, 8 MB PSRAM OPI) |
| Pantalla | RGB 800x480 (ST7262) |
| Táctil | GT911 por I2C (SDA 8, SCL 9, INT 4) |
| Expansor IO | CH422G: reset del táctil y del LCD, retroiluminación, selector USB/CAN |
| CAN | TJA1051, TWAI: TX GPIO20, RX GPIO19 |

**Importante:** en este kit el CAN comparte pines con el USB nativo. El firmware activa el
modo CAN (EXIO5 = 1), así que el monitor serie va por el **puerto UART** (USB-UART), no por el
USB nativo.

### Conexión al bus CAN
- Conecta `CANH` y `CANL` del kit al bus (por ejemplo, pines 6 y 14 del conector OBD-II).
- Comparte la masa (GND).
- Si el kit está en un extremo del bus, activa la resistencia de terminación de 120 Ω.

### Señales decodificadas

Las señales vienen de IDs estándar de 11 bits. El modo demo usa estas mismas IDs:

| ID | Señal |
|----|-------|
| `0x100` | rpm (bytes 0-1), marcha (byte 2) |
| `0x101` | velocidad, en 0,01 km/h (bytes 0-1) |
| `0x102` | temperatura (byte 0, con +40 de offset), combustible en % (byte 1), batería en mV (bytes 2-3) |
| `0x103` | luces (bitmask) |
| `0x7E8` | respuestas OBD-II, modo 01: rpm, velocidad y temperatura |

---

## Estructura del proyecto

```
firmware/              Proyecto PlatformIO (framework Arduino)
  platformio.ini
  include/             Cabeceras y secrets.example.h
  src/                 Código: pantalla, táctil, CH422G, CAN, simulador, WiFi, web
  data/                App web (index.html, app.css, app.js) → LittleFS
tools/mock_server.py   Servidor en PC que imita al ESP32, para probar la web sin hardware
docs/SPEC.md           Especificación compartida firmware/web
```

---

## Instalación y carga en el ESP32

### 1. Requisitos
- [PlatformIO](https://platformio.org/): `pip install platformio`, o la extensión de VS Code.
- Cable USB al puerto **UART** del kit.

### 2. WiFi (opcional)
```bash
cp firmware/include/secrets.example.h firmware/include/secrets.h
# edita secrets.h con el SSID y la clave de tu red
```
Sin `secrets.h`, o si la conexión falla en 10 s, el ESP32 crea su propia red:
- **SSID:** `MocciaCAN`
- **Clave:** `moccia1234`
- **IP:** `192.168.4.1`

### 3. Compilar y grabar
```bash
cd firmware
pio run -t upload        # graba el firmware
pio run -t uploadfs      # graba la app web en LittleFS
pio device monitor       # monitor serie (115200)
```
Graba las dos cosas: sin `uploadfs`, la pantalla funciona pero la web no.

### 4. Usar la app web
Conéctate a la misma red que el ESP32 y abre cualquiera de estas direcciones:
- `http://moccia.local` (mDNS)
- La IP que se ve en **Ajustes** en la pantalla del kit (`http://192.168.4.1` en modo AP)

En el móvil puedes usar "Añadir a pantalla de inicio" para instalarla como app.

---

## Probar la web sin hardware

```bash
pip install aiohttp
python tools/mock_server.py --port 8080
# abre http://localhost:8080
```
El servidor de pruebas imita al ESP32: mismos endpoints, mismo WebSocket y datos simulados.

---

## API

| Método | Ruta | Descripción |
|--------|------|-------------|
| GET | `/` | App web |
| GET | `/api/state` | Estado actual (JSON) |
| GET | `/api/settings` | Ajustes |
| POST | `/api/settings` | Cambia ajustes (JSON parcial, p. ej. `{"demo":false}`) |
| WS | `/ws` | Tiempo real (ver protocolo abajo) |

### Protocolo WebSocket

**Del servidor al cliente:**

| Mensaje | Cuándo se envía |
|---------|-----------------|
| `state` | 10 veces por segundo |
| `frames` | cada 500 ms |
| `history` | al conectar |
| `sample` | cada 1 s |
| `settings` | al conectar y con cada cambio |
| `sys` | al conectar y cada 5 s |

**Del cliente al servidor:**
- `{"t":"set","k":"bitrate|demo|backlight|units","v":...}`
- `{"t":"cmd","c":"pause|resume|clear"}`

Los ajustes se guardan en la memoria NVS y se mantienen tras reiniciar.

Detalle completo de cada campo: [`docs/SPEC.md`](docs/SPEC.md).

---

## Solución de problemas

| Problema | Solución |
|----------|----------|
| Pantalla en negro | Comprueba que la placa sea la variante de **7"**. El firmware enciende la retroiluminación con el CH422G al arrancar. |
| No aparece el puerto serie | Usa el conector **UART**. El USB nativo se desactiva porque el CAN usa esos pines. |
| CAN "SIN TRÁFICO" | Revisa el bitrate en Ajustes (normalmente 500k en vehículos), el cableado CANH/CANL y la terminación. |
| CAN "BUS-OFF" | Bitrate incorrecto o bus sin terminar. El firmware se recupera solo. |
| La web no carga | Falta `pio run -t uploadfs`. |
