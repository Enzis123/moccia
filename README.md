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

## Qué hace y cómo funciona

### En pocas palabras

MocciaCAN convierte el kit en un **panel de coche**. Escucha lo que se dicen los aparatos del coche por el **bus CAN** y lo enseña de dos formas a la vez:

1. En la **pantalla táctil** del propio kit.
2. En una **página web** que se abre desde el móvil o el PC, sin internet, conectándose directamente al WiFi del kit.

Las dos muestran **lo mismo y al mismo tiempo**.

### ¿Qué es el bus CAN?

Dentro de un coche hay muchos "cerebros" electrónicos: el del motor, el de los frenos, el del tablero, etc. Se comunican por **dos cables** compartidos, como si todos hablaran en el mismo grupo de chat. Ese par de cables es el **bus CAN**.

Cada mensaje se llama **trama** y lleva:
- un **ID**, que dice de qué trata el mensaje (por ejemplo `0x100`);
- hasta **8 bytes de datos**, que son los números en sí (por ejemplo, las revoluciones del motor).

MocciaCAN se conecta a esos dos cables y **solo escucha**: lee todos los mensajes pero nunca envía nada, así que no puede molestar al coche.

### Las piezas del sistema

```
   Coche (bus CAN)                    Kit ESP32-S3                      Móvil / PC
  ┌──────────────┐   2 cables    ┌─────────────────────────┐   WiFi   ┌──────────────┐
  │ motor, ABS,  │ ────────────► │ 1. Lee las tramas CAN   │ ───────► │  Navegador   │
  │ tablero...   │  CANH / CANL  │ 2. Saca los valores     │          │  192.168.4.1 │
  └──────────────┘               │ 3. Los pinta en pantalla│ ◄─────── │ (botón demo) │
                                 │ 4. Sirve la página web  │          └──────────────┘
                                 └─────────────────────────┘
```

El **ESP32-S3** es el chip del kit: un microcontrolador con WiFi incluido. El programa repite estos pasos sin parar, muchas veces por segundo:

| Paso | Qué hace | Cada cuánto |
|------|----------|-------------|
| 1. **Leer el CAN** | Recoge las tramas que han llegado por los cables | Constantemente |
| 2. **Interpretar** | Si la trama es una conocida (ID `0x100`, `0x101` o `0x102`), saca de ella la velocidad, las rpm, la temperatura o la batería. Además la apunta en la tabla de la página CAN | Con cada trama |
| 3. **Mirar el táctil** | Comprueba si has tocado una pestaña o el botón de demo | Cada 30 ms |
| 4. **Dibujar** | Actualiza los números de la página que estás viendo | Cada 250 ms |
| 5. **Atender la web** | Responde al navegador con los datos actuales | Cuando el navegador pregunta |

### Cómo se mantienen iguales la pantalla y la web

El kit guarda **una sola copia** de los datos (velocidad, rpm, tramas…). Tanto la pantalla como la web leen de esa misma copia, por eso siempre coinciden:

- La **pantalla** se redibuja ella sola cada 250 ms.
- La **página web** pregunta al kit cada medio segundo "¿qué datos tienes?" (en la dirección `/datos`). El kit le contesta con los valores actuales y la página los muestra.
- Si pulsas **Modo demo** en la web, el navegador se lo dice al kit (en `/demo`). El kit cambia el modo y, como la pantalla lee de la misma copia, también cambia en la pantalla. Y al revés: si lo cambias tocando la pantalla, la web lo verá en su siguiente pregunta.

La página web **va guardada dentro del propio programa** (archivo `pagina_web.h`), así que no hace falta internet ni grabar nada aparte: el kit es a la vez el aparato y el servidor web.

### El modo demo

Sin un coche conectado no habría nada que ver, así que el **modo demo** se inventa un coche circulando:
- la velocidad sube y baja suavemente entre 10 y 110 km/h;
- las revoluciones siguen a la velocidad;
- el motor se calienta poco a poco hasta 90 °C;
- la batería oscila alrededor de 13,9 V.

Lo interesante es que el modo demo **fabrica tramas CAN de verdad** (con las IDs `0x100`, `0x101` y `0x102`) y las mete por el mismo camino que las del coche real. Así, lo que ves en demo es exactamente cómo se vería con un coche que enviara esos mensajes. Al quitar el modo demo, el kit pasa a mostrar solo lo que llegue por los cables.

### Las 3 páginas

| Página | Qué enseña | Para qué sirve |
|--------|------------|----------------|
| **Tablero** | Velocidad, revoluciones, temperatura y batería, con un número grande y una barra | Ver de un vistazo el estado del coche |
| **CAN** | Una tabla con los últimos 8 tipos de mensaje recibidos: su ID, cuántos bytes trae, los datos en hexadecimal y cuántas veces ha llegado. Arriba, cuántos mensajes llegan por segundo y el total | Ver qué pasa por el bus aunque el kit no entienda los mensajes; útil para investigar un coche |
| **Ajustes** | Botón de modo demo, nombre y clave del WiFi, dirección web, versión y tiempo encendido | Configurar y saber cómo conectarse |

### Resumen para explicarlo en una frase

> "Es un panel táctil que escucha la red interna del coche (el bus CAN), muestra la velocidad, las revoluciones, la temperatura y la batería, y además crea su propio WiFi para que puedas ver lo mismo en el móvil. Tiene un modo demo que simula un coche para enseñarlo sin tener uno."

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
