# MocciaCAN (versión simple)

Panel para el kit **Waveshare ESP32-S3-Touch-LCD-7** (pantalla táctil de 7", 800x480, con CAN).

- **En la pantalla:** 3 páginas que cambias tocando las pestañas de arriba.
  - **Tablero:** velocidad, revoluciones, temperatura y batería.
  - **CAN:** las tramas que pasan por el bus.
  - **Ajustes:** modo demo y datos de la red WiFi.
- **En el móvil o el PC:** la placa crea su propia red WiFi y muestra **lo mismo** en el navegador.
- **Modo demo** (activado al encender): inventa datos para que lo veas funcionar sin coche.

Todo el programa está en la carpeta `MocciaCAN/`:

| Archivo | Qué es |
|---------|--------|
| `MocciaCAN.ino` | El programa de la placa |
| `pagina_web.h` | La página web, ya incluida en el programa (no se graba aparte) |

---

## Cómo grabarlo (Arduino IDE)

### 1. Prepara el Arduino IDE (solo la primera vez)
1. Instala el **[Arduino IDE 2](https://www.arduino.cc/en/software)**.
2. Abre **Archivo → Preferencias** y, en *Gestor de URLs adicionales de tarjetas*, pega:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. Abre **Herramientas → Placa → Gestor de tarjetas**, busca **esp32** (de Espressif) e instala la versión **2.0.17**.
   > Elige exactamente la 2.0.17. Con las versiones 3.x la pantalla no compila.
4. Abre **Herramientas → Gestionar bibliotecas**, busca **LovyanGFX** e instala la versión **1.1.16**.

### 2. Abre el programa
Descarga el proyecto (en GitHub: **Code → Download ZIP**) y descomprímelo. Abre `MocciaCAN/MocciaCAN.ino` con el Arduino IDE.

### 3. Elige la placa y sus opciones (menú Herramientas)

| Opción | Valor |
|--------|-------|
| Placa | **ESP32S3 Dev Module** |
| Flash Size | **16MB (128Mb)** |
| PSRAM | **OPI PSRAM** |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** |
| USB CDC On Boot | **Disabled** |
| Puerto | El COM o `/dev/tty…` que aparece al conectar la placa |

### 4. Graba
1. Conecta la placa por el USB marcado **UART**.
2. Pulsa la flecha **Subir** (→).
3. Si no conecta, mantén pulsado **BOOT**, pulsa y suelta **RESET**, suelta **BOOT** y vuelve a pulsar **Subir**.
4. Cuando termine, pulsa **RESET**.

---

## Cómo usarlo

1. Al encender, la pantalla muestra el **Tablero** con datos de demostración. Toca **CAN** o **Ajustes** arriba para cambiar de página.
2. Con el móvil o el PC, conéctate a la red WiFi **`MocciaCAN`** (clave **`moccia1234`**).
3. Abre en el navegador **`http://192.168.4.1`**. Verás las mismas 3 páginas con los mismos datos.
4. El botón **Modo demo** (en Ajustes) funciona desde la pantalla y desde la web; si lo cambias en un sitio, cambia en el otro.

## Conectarlo a un bus CAN

- Conecta **CANH**, **CANL** y **GND** del kit al bus. En un conector OBD-II del coche: CANH es el pin 6, CANL el 14 y GND los pines 4 y 5.
- En **Ajustes**, pon **Modo demo: NO**.
- La placa escucha a **500 kbit/s** en modo **solo escucha**: nunca envía nada al bus, así que no molesta a otros equipos.

La página **CAN** muestra cualquier trama que llegue. El **Tablero** solo entiende estas IDs (puedes cambiarlas en la función `procesarTrama` del `.ino`):

| ID | Datos |
|----|-------|
| `0x100` | bytes 0-1: revoluciones (rpm) |
| `0x101` | bytes 0-1: velocidad en centésimas de km/h |
| `0x102` | byte 0: temperatura + 40 · bytes 2-3: batería en milivoltios |

Los coches usan cada uno sus propias IDs, así que con un coche real lo normal es que solo se llene la página CAN.

## Cambiar el nombre o la clave del WiFi

Al principio de `MocciaCAN.ino`:
```cpp
const char* WIFI_NOMBRE = "MocciaCAN";
const char* WIFI_CLAVE  = "moccia1234";   // mínimo 8 caracteres
```

## Si algo falla

| Problema | Solución |
|----------|----------|
| Error al compilar con la pantalla | Revisa que la placa esp32 sea la **2.0.17** y LovyanGFX la **1.1.16** |
| No sube el programa | Usa el USB **UART** y haz lo de BOOT + RESET |
| Pantalla negra | Revisa la opción **PSRAM: OPI PSRAM** |
| La imagen tiembla o se desplaza | En el `.ino`, cambia `cfg.freq_write = 16000000;` por `14000000` |
| La web no abre | Comprueba que el móvil esté conectado a la red `MocciaCAN` y abre `http://192.168.4.1` |
| La página CAN no muestra nada | Pon el modo demo en NO, revisa CANH/CANL/GND y que el bus vaya a 500 kbit/s |

---

GitHub compila el programa automáticamente en cada cambio (pestaña **Actions**) para comprobar que no tiene errores.
