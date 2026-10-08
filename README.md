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

**Modo demo:** si está activo, el ESP32 genera una conducción realista y la emite como tramas
CAN simuladas (con los IDs de la tabla de abajo) que pasan por el mismo decodificador que las
reales: el tablero, la tabla de tramas, tramas/s y las gráficas se mueven, y el bus aparece como
**OK**. Así se puede probar la app sin conectar el kit a un vehículo.

Con el modo demo **desactivado** y sin tráfico real (más de 2 s sin tramas), los valores del
tablero se siguen simulando para que la pantalla no quede congelada, pero el bus aparece como
**SIN TRÁFICO**, tramas/s es 0 y la tabla de tramas no cambia. En cuanto llegan tramas reales,
el tablero muestra los valores decodificados del bus.

**Monitor CAN:** la tabla guarda como máximo 12 IDs distintas, ordenadas por ID; si llega una
nueva con la tabla llena, se sustituye la que lleva más tiempo sin verse. En pausa la tabla se
congela (tramas/s y el tablero siguen actualizándose). "Limpiar" vacía la tabla en todas las
pantallas a la vez.

**Retroiluminación:** el hardware solo permite encenderla o apagarla (CH422G). Si se apaga desde
Ajustes (en la pantalla o en la web), **un toque en la pantalla la vuelve a encender** (ese primer
toque no pulsa ningún botón). También se puede encender desde la web.

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
- El firmware **nunca transmite tramas** (no envía peticiones OBD-II: solo decodifica las
  respuestas `0x7E8` que pida otro equipo). El controlador TWAI funciona en modo normal, así que
  **sí confirma (ACK)** las tramas y genera tramas de error si el bitrate es incorrecto.
  Configura el bitrate correcto en Ajustes **antes** de conectarlo a un vehículo.

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
firmware/                 Proyecto PlatformIO (framework Arduino)
  platformio.ini
  include/                Cabeceras (*.h) y secrets.example.h
  src/                    Código del firmware (ver "Arquitectura")
  data/                   App web → LittleFS: index.html, app.css, app.js, manifest.json, icon.svg
tools/mock_server.py      Servidor en PC que imita al ESP32, para probar la web sin hardware
tools/web_smoke_test.py   Prueba automática de la web (Playwright) contra el mock o el ESP32
docs/SPEC.md              Especificación compartida firmware/web
.github/workflows/build.yml  Integración continua (ver "Verificación")
```

---

## Arquitectura del firmware

| Módulo (`src/` + `include/`) | Función |
|------------------------------|---------|
| `main.cpp` | Arranque: carga ajustes, CH422G, pantalla, CAN y, en segundo plano, WiFi + servidor web |
| `app_config.h` | Pines, temporización del LCD, direcciones I2C, colores y constantes |
| `state.cpp/.h` | Estado compartido (datos del vehículo, tabla de tramas, historial de 120 s), protegido por un mutex |
| `app_settings.cpp/.h` | Ajustes persistentes en NVS (bitrate, demo, retroiluminación, unidades) |
| `ch422g.cpp/.h` | Expansor CH422G: resets de LCD y táctil, retroiluminación, USB_SEL = CAN |
| `display.h` | Configuración LovyanGFX: panel RGB 800x480 + táctil GT911 |
| `ui.cpp/.h` | Interfaz táctil: barra superior y las 4 páginas (dibujadas con sprites en PSRAM) |
| `can_bus.cpp/.h` | Driver TWAI (cambio de bitrate, recuperación de BUS-OFF), decodificación, tramas/s, historial |
| `simulator.cpp/.h` | Simulador de conducción que genera tramas CAN (modo demo) |
| `wifi_net.cpp/.h` | WiFi STA con `secrets.h` (opcional), AP de respaldo y mDNS `moccia.local` |
| `web.cpp/.h` | Servidor HTTP (LittleFS + API REST) y WebSocket `/ws` |

Tareas FreeRTOS:
- **loopTask** (núcleo 1): interfaz táctil. Es la única que usa LovyanGFX y el bus I2C
  (táctil y CH422G).
- **can** (núcleo 0): recepción TWAI, simulador, tramas/s e historial (1 muestra/s).
- **web** (núcleo 0): difusión por WebSocket (estado 10 Hz, tramas 2 Hz, muestra 1 Hz,
  sistema cada 5 s) y difusión inmediata cuando cambian los ajustes o se usa Pausar/Limpiar.
- **async_tcp**: atiende HTTP y los mensajes WebSocket entrantes. Solo modifica el estado
  compartido (con mutex) y los ajustes. La pantalla aplica los cambios (por ejemplo, la
  retroiluminación) desde su propia tarea.

Cualquier cambio, hecho con el táctil o desde la web, incrementa un contador de revisión. La
tarea **web** lo detecta y lo difunde a todos los clientes.

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
Con el modo demo desactivado se comporta como un ESP32 sin tráfico: bus SIN TRÁFICO,
0 tramas/s y tabla congelada.

Prueba automática (abre dos navegadores y comprueba datos, pestañas, sincronización de ajustes
y Pausar/Limpiar):
```bash
pip install playwright && python -m playwright install chromium
python tools/web_smoke_test.py --url http://localhost:8080   # o la IP del ESP32
```

---

## Verificación (CI)

El workflow `.github/workflows/build.yml` se ejecuta en cada *pull request*, en cada *push* a
`main` y a mano (*workflow_dispatch*). Tiene dos trabajos:
- **firmware**: instala PlatformIO, compila el firmware (`pio run`) y genera la imagen LittleFS
  de la web (`pio run -t buildfs`). Los `.bin` quedan como artefacto descargable
  (`moccia-firmware`).
- **web**: instala `aiohttp`, Playwright y Chromium (`python -m playwright install --with-deps
  chromium`), arranca `tools/mock_server.py`, espera a que `/api/state` responda y ejecuta
  `tools/web_smoke_test.py`.

La CI no prueba el hardware real (pantalla, táctil, CAN). Para eso, sigue la sección de
instalación y revisa las cuatro páginas en el kit.

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

La velocidad viaja siempre en km/h. Cada cliente la convierte a mph si el ajuste `units` es
`mph`. `heap` y `psram` van en bytes y `rssi` en dBm. `history` contiene exactamente 120 valores,
del más antiguo al más reciente. Tras cualquier `cmd`, el servidor difunde `frames` y `state` al
momento. En pausa, `frames` se sigue enviando, pero con la tabla congelada.

Detalle completo de cada campo: [`docs/SPEC.md`](docs/SPEC.md).

---

## Solución de problemas

| Problema | Solución |
|----------|----------|
| Pantalla en negro | Si la retroiluminación se apagó desde Ajustes, toca la pantalla o actívala desde la web (se guarda en NVS). Comprueba también que la placa sea la variante de **7"**. |
| No aparece el puerto serie | Usa el conector **UART**. El USB nativo se desactiva porque el CAN usa esos pines. |
| CAN "SIN TRÁFICO" | Revisa el bitrate en Ajustes (normalmente 500k en vehículos), el cableado CANH/CANL y la terminación. |
| CAN "BUS-OFF" | Bitrate incorrecto o bus sin terminar. El firmware se recupera solo. |
| La web no carga | Falta `pio run -t uploadfs`. |
