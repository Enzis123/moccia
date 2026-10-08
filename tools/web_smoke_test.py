#!/usr/bin/env python3
"""Prueba de humo de la app web contra el mock (o contra el ESP32 real).

Requisitos:  pip install playwright   (y un Chromium: `playwright install chromium`)
Uso:
  python tools/mock_server.py --port 8080 &
  python tools/web_smoke_test.py --url http://localhost:8080 [--shots DIR] [--chromium RUTA]

Comprueba: sin errores de consola, datos que se actualizan, las 4 pestañas,
sincronización de ajustes entre dos clientes y Pausar/Limpiar del monitor CAN.
"""
import argparse
import os
import sys
import time

from playwright.sync_api import sync_playwright

PAGES = ["tablero", "can", "graficas", "ajustes"]
SIZES = {"800x480": (800, 480), "390x844": (390, 844), "1280x800": (1280, 800)}

failures = []


def check(cond, msg):
    print(("  OK   " if cond else "  FAIL ") + msg)
    if not cond:
        failures.append(msg)


def open_page(browser, url, w, h, errors):
    ctx = browser.new_context(viewport={"width": w, "height": h}, device_scale_factor=2 if w < 500 else 1)
    page = ctx.new_page()
    page.on("console", lambda m: m.type == "error" and errors.append(m.text))
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(url)
    page.wait_for_selector("#link.ok", timeout=8000)
    return ctx, page


def frame_counts(page):
    return page.eval_on_selector_all("#fBody tr td:nth-child(4)", "els => els.map(e => e.textContent)")


def canvas_has_ink(page):
    return page.evaluate("""() => [...document.querySelectorAll('.chart canvas')].map(c => {
        const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
        let n = 0; for (let i = 3; i < d.length; i += 4) if (d[i]) n++;
        return n > 500; })""")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:8080/")
    ap.add_argument("--shots", default="")
    ap.add_argument("--chromium", default=os.environ.get("CHROMIUM", ""))
    a = ap.parse_args()

    errors = []
    with sync_playwright() as p:
        kw = {"executable_path": a.chromium} if a.chromium else {}
        browser = p.chromium.launch(**kw)
        ctx1, p1 = open_page(browser, a.url, 800, 480, errors)

        print("Datos en vivo")
        up0 = p1.text_content("#uptime")
        sp0 = p1.text_content("#gSpeed .g-v")
        time.sleep(2.2)
        check(p1.text_content("#uptime") != up0, f"uptime avanza ({up0} -> {p1.text_content('#uptime')})")
        check(p1.text_content("#gSpeed .g-v") not in ("--", ""), f"velocidad mostrada ({sp0})")
        check(p1.text_content("#gRpm .g-v") not in ("--", ""), "rpm mostradas")
        check(p1.text_content("#gear") != "-", "marcha mostrada")
        check(p1.is_hidden("#banner"), "sin banner de desconexión")

        print("Pestañas")
        for name in PAGES:
            p1.click(f".tabs button[data-p={name}]")
            check(p1.is_visible(f"#p-{name}") and all(
                p1.is_hidden(f"#p-{o}") for o in PAGES if o != name), f"pestaña {name} visible")
            if name == "can":
                p1.wait_for_selector("#fBody tr", timeout=3000)
                check(len(frame_counts(p1)) > 0, f"tabla CAN con {len(frame_counts(p1))} IDs")
                check(p1.text_content("#cFps") not in ("--", "0"), "tramas/s > 0")
            if name == "graficas":
                time.sleep(0.3)
                check(all(canvas_has_ink(p1)), "las 3 gráficas dibujadas")
            if name == "ajustes":
                check(p1.text_content("#iVer") != "--", "info del sistema recibida")

        print("Sincronización entre clientes")
        ctx2, p2 = open_page(browser, a.url, 390, 844, errors)
        p2.click(".tabs button[data-p=ajustes]")
        p1.click("#sUnits button[data-v=mph]")
        p2.wait_for_selector("#sUnits button[data-v=mph].on", state="attached", timeout=3000)
        check(True, "unidades mph reflejadas en el segundo cliente")
        before = p1.get_attribute(".sw[data-k=demo]", "aria-checked")
        p2.click(".sw[data-k=demo]")
        p1.wait_for_function(f"document.querySelector('.sw[data-k=demo]').getAttribute('aria-checked') !== '{before}'", timeout=3000)
        check(True, "modo demo cambiado desde el segundo cliente se refleja en el primero")
        p2.click(".sw[data-k=demo]")
        p2.click(".tabs button[data-p=tablero]")
        check(p2.text_content("#gSpeed .g-u") == "mph", "velocímetro en mph en el segundo cliente")
        p1.click("#sUnits button[data-v=kmh]")
        p1.click("#sBitrate button[data-v='250000']")
        p2.wait_for_selector("#sBitrate button[data-v='250000'].on", state="attached", timeout=3000)
        check(True, "bitrate 250k reflejado")
        p1.click("#sBitrate button[data-v='500000']")
        p2.wait_for_selector("#sBitrate button[data-v='500000'].on", state="attached", timeout=3000)

        print("Pausar / Limpiar")
        p1.click(".tabs button[data-p=can]")
        p2.click(".tabs button[data-p=can]")
        p1.click("#btnPause")
        p2.wait_for_function("document.getElementById('btnPause').textContent === 'Reanudar'", timeout=3000)
        check(True, "pausa reflejada en el segundo cliente")
        time.sleep(0.6)
        c0 = frame_counts(p1)
        time.sleep(1.2)
        check(frame_counts(p1) == c0, "contadores congelados en pausa")
        p2.click("#btnClear")
        p1.wait_for_selector("#fEmpty", state="visible", timeout=3000)
        check(len(frame_counts(p1)) == 0, "tabla vacía tras Limpiar")
        p1.click("#btnPause")
        p1.wait_for_selector("#fBody tr", timeout=3000)
        check(p1.text_content("#btnPause") == "Pausar", "monitor reanudado y tabla repoblada")
        ctx2.close()

        ctx1.close()

        if a.shots:
            os.makedirs(a.shots, exist_ok=True)
            for label, (w, h) in SIZES.items():
                ctx, pg = open_page(browser, a.url, w, h, errors)
                time.sleep(1.2)
                for name in PAGES:
                    pg.click(f".tabs button[data-p={name}]")
                    time.sleep(0.6)
                    path = os.path.join(a.shots, f"{label}_{name}.png")
                    pg.screenshot(path=path, full_page=True)
                ctx.close()
            print(f"Capturas en {a.shots}")
        browser.close()

    check(not errors, "sin errores de consola" + ("" if not errors else f": {errors[:3]}"))
    print("\nRESULTADO:", "OK" if not failures else f"{len(failures)} fallos")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
