#!/usr/bin/env python3
"""Servidor de pruebas que imita al ESP32 de MocciaCAN (ver docs/SPEC.md).

Sirve la app web de firmware/data/ y expone la misma API HTTP y el mismo
WebSocket que el firmware, con datos de conducción simulados.

Requisitos:  pip install aiohttp
Uso:         python tools/mock_server.py --port 8080   ->  http://localhost:8080
"""
import argparse
import asyncio
import json
import math
import random
import socket
import time
from collections import deque
from pathlib import Path

from aiohttp import WSMsgType, web

DATA_DIR = Path(__file__).resolve().parent.parent / "firmware" / "data"
VERSION = "1.0.0"
HIST_LEN = 120
MAX_IDS = 12
BITRATES = (125000, 250000, 500000, 1000000)
GEARS = ["P", "R", "N", "D", "1", "2", "3", "4", "5", "6"]  # código CAN 0..9
# Relación rpm por km/h para cada marcha 1..6
RATIOS = {1: 120.0, 2: 75.0, 3: 52.0, 4: 40.0, 5: 32.0, 6: 26.0}
UPSHIFT = [0, 18, 34, 54, 76, 98]  # velocidad para pasar a la marcha i+1


def local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"


def hexbytes(b):
    return " ".join(f"{x:02X}" for x in b)


class Sim:
    """Simulador de vehículo + bus CAN."""

    def __init__(self):
        self.t0 = time.monotonic()
        self.settings = {"bitrate": 500000, "demo": True, "backlight": True, "units": "kmh"}
        self.paused = False
        # vehículo
        self.speed = 0.0
        self.target = 0.0
        self.next_target_at = 0.0
        self.gear_n = 0  # 0 = parado; 1..6
        self.gear = "P"
        self.parked_for = 10.0
        self.rpm = 0
        self.temp = 24.0
        self.fuel = 72.0
        self.batt = 12.5
        self.turn = 0  # 0 nada, 1 izq, 2 dcha
        self.turn_until = 0.0
        self.next_turn_at = 15.0
        self.check_until = 0.0
        self.lights = 0
        self.sim_t = 0.0
        # bus
        self.bus_ids = {  # id -> periodo nominal (ms)
            0x100: 10, 0x101: 20, 0x102: 100, 0x103: 100, 0x1A0: 50,
            0x2F0: 200, 0x3E9: 500, 0x420: 1000, 0x5A1: 250, 0x7E8: 1000,
        }
        self.acc = {i: 0.0 for i in self.bus_ids}
        self.rolling = 0
        self.table = {}  # id -> dict(dlc, data, count, period, last)
        self.fps = 0
        self.hist = {k: deque([0] * HIST_LEN, maxlen=HIST_LEN) for k in ("speed", "rpm", "temp")}
        self.rssi = -58

    # ---- vehículo ---------------------------------------------------------
    def step_vehicle(self, dt):
        self.sim_t += dt
        t = self.sim_t
        if t >= self.next_target_at:
            self.target = random.choice([0, 0, 30, 50, 50, 80, 100, 120, 135])
            self.next_target_at = t + random.uniform(8, 18)
        diff = self.target - self.speed
        accel = 9.0 if diff > 0 else 12.0
        self.speed += max(-accel * dt, min(accel * dt, diff))
        self.speed = max(0.0, self.speed + random.uniform(-0.15, 0.15) * (self.speed > 1))
        accelerating = diff > 2

        if self.speed < 0.5:
            self.speed = 0.0
            self.parked_for += dt
            self.gear_n = 0
            self.gear = "P" if self.parked_for > 6 else "N"
            self.rpm = int(780 + random.uniform(-25, 25))
        else:
            self.parked_for = 0.0
            g = max(1, self.gear_n)
            while g < 6 and self.speed > UPSHIFT[g]:
                g += 1
            while g > 1 and self.speed < UPSHIFT[g - 1] - 8:
                g -= 1
            self.gear_n = g
            self.gear = str(g)
            base = self.speed * RATIOS[g] * (1.12 if accelerating else 1.0)
            self.rpm = int(max(850, min(7600, base + random.uniform(-40, 40))))

        self.temp = min(90.0 + math.sin(t / 25) * 2.5, self.temp + 0.6 * dt) + random.uniform(-0.05, 0.05)
        self.fuel -= dt * (0.004 + self.rpm / 1.5e6)
        if self.fuel < 5:
            self.fuel = 100.0
        self.batt = 14.1 + math.sin(t / 7) * 0.08 + random.uniform(-0.02, 0.02)

        if t >= self.next_turn_at and self.speed > 10:
            self.turn = random.choice([1, 2])
            self.turn_until = t + random.uniform(3, 6)
            self.next_turn_at = t + random.uniform(15, 30)
        if t > self.turn_until:
            self.turn = 0
        if random.random() < dt / 150:
            self.check_until = t + 8
        lights = 0x01  # cortas siempre
        if self.speed > 95:
            lights |= 0x02
        if self.turn and int(t * 3) % 2 == 0:  # parpadeo ~1.5 Hz
            lights |= 0x04 if self.turn == 1 else 0x08
        if self.gear == "P":
            lights |= 0x10
        if t < self.check_until:
            lights |= 0x20
        self.lights = lights

    # ---- tramas CAN ----------------------------------------------------------
    def payload(self, cid):
        if cid == 0x100:
            r = self.rpm
            return [r >> 8, r & 0xFF, GEARS.index(self.gear), 0, 0, 0, 0, 0]
        if cid == 0x101:
            v = int(self.speed * 100)
            return [v >> 8 & 0xFF, v & 0xFF, 0, 0, 0, 0, 0, 0]
        if cid == 0x102:
            mv = int(self.batt * 1000)
            return [int(self.temp) + 40, int(self.fuel), mv >> 8, mv & 0xFF]
        if cid == 0x103:
            return [self.lights]
        if cid == 0x7E8:
            r = self.rpm * 4
            return [0x04, 0x41, 0x0C, r >> 8 & 0xFF, r & 0xFF, 0x55, 0x55, 0x55]
        self.rolling = (self.rolling + 1) & 0xFF
        if cid == 0x1A0:
            ang = int(math.sin(self.sim_t / 3) * 900) & 0xFFFF
            return [ang >> 8, ang & 0xFF, self.rolling, 0x00, 0x10, 0x00]
        if cid == 0x2F0:
            return [random.randint(0, 255) for _ in range(8)]
        if cid == 0x420:
            return [0x01, 0x00, self.rolling & 0x0F]
        return [self.rolling, 0xA5, 0x5A, random.randint(0, 255), 0, 0, 0, 0]

    def step_bus(self, dt):
        if not self.settings["demo"]:
            # Sin bus real en el PC: sin tráfico (los datos del tablero se siguen simulando).
            self.fps = 0
            return
        now = time.monotonic()
        total = 0
        for cid, per in self.bus_ids.items():
            self.acc[cid] += dt * 1000.0 / per
            n = int(self.acc[cid])
            self.acc[cid] -= n
            total += n
            if n and not self.paused:
                data = self.payload(cid)
                e = self.table.get(cid)
                if e is None and len(self.table) >= MAX_IDS:
                    oldest = min(self.table, key=lambda k: self.table[k]["last"])
                    del self.table[oldest]
                    e = None
                if e is None:
                    e = self.table[cid] = {"count": 0, "period": per, "last": now}
                e["dlc"] = len(data)
                e["data"] = hexbytes(data)
                e["count"] += n
                e["period"] = max(1, int(round(per + random.uniform(-0.4, 0.4) * per / 10)))
                e["last"] = now
        self.fps = int(total / dt) if dt > 0 else 0

    def bus_state(self):
        return "OK" if self.settings["demo"] else "IDLE"

    # ---- mensajes --------------------------------------------------------------
    def uptime(self):
        return int(time.monotonic() - self.t0)

    def state(self):
        return {
            "rpm": self.rpm, "speed": round(self.speed, 2), "temp": int(round(self.temp)),
            "fuel": int(self.fuel), "batt": round(self.batt, 2), "gear": self.gear,
            "lights": self.lights, "fps": self.fps, "bus": self.bus_state(),
            "demo": self.settings["demo"], "paused": self.paused, "uptime": self.uptime(),
        }

    def frames(self):
        rows = sorted(self.table.items())
        return [{"id": f"0x{cid:03X}", "dlc": e["dlc"], "data": e["data"],
                 "count": e["count"], "period": e["period"]} for cid, e in rows]

    def sample(self):
        s = {"speed": round(self.speed, 1), "rpm": self.rpm, "temp": int(round(self.temp))}
        for k, v in s.items():
            self.hist[k].append(v)
        return s

    def history(self):
        return {k: list(v) for k, v in self.hist.items()}

    def sys(self, ip):
        self.rssi = max(-80, min(-45, self.rssi + random.randint(-2, 2)))
        return {"ip": ip, "ssid": "MockNet", "rssi": self.rssi,
                "heap": 182000 + random.randint(-3000, 3000),
                "psram": 7_800_000 + random.randint(-20000, 20000),
                "ver": VERSION, "mode": "STA"}

    def apply_set(self, k, v):
        """Aplica un ajuste; devuelve True si cambió algo válido."""
        if k == "bitrate":
            try:
                v = int(v)
            except (TypeError, ValueError):
                return False
            if v not in BITRATES:
                return False
        elif k in ("demo", "backlight"):
            if not isinstance(v, bool):
                return False
        elif k == "units":
            if v not in ("kmh", "mph"):
                return False
        else:
            return False
        self.settings[k] = v
        return True

    def apply_cmd(self, c):
        if c == "pause":
            self.paused = True
        elif c == "resume":
            self.paused = False
        elif c == "clear":
            self.table.clear()
        else:
            return False
        return True


class App:
    def __init__(self, ip):
        self.sim = Sim()
        self.ip = ip
        self.clients = set()
        # Precalcular 120 s de conducción para que las gráficas no empiecen vacías.
        for i in range(HIST_LEN * 10):
            self.sim.step_vehicle(0.1)
            if i % 10 == 9:
                self.sim.sample()

    @staticmethod
    def msg(t, d):
        return json.dumps({"t": t, "d": d}, separators=(",", ":"))

    async def broadcast(self, text):
        dead = []
        for ws in list(self.clients):
            try:
                await ws.send_str(text)
            except (ConnectionError, RuntimeError):
                dead.append(ws)
        for ws in dead:
            self.clients.discard(ws)

    async def loop(self):
        tick = 0
        last = time.monotonic()
        while True:
            await asyncio.sleep(0.1)
            now = time.monotonic()
            dt, last = now - last, now
            self.sim.step_vehicle(dt)
            self.sim.step_bus(dt)
            tick += 1
            await self.broadcast(self.msg("state", self.sim.state()))
            if tick % 5 == 0:
                await self.broadcast(self.msg("frames", self.sim.frames()))
            if tick % 10 == 0:
                await self.broadcast(self.msg("sample", self.sim.sample()))
            if tick % 50 == 0:
                await self.broadcast(self.msg("sys", self.sim.sys(self.ip)))

    async def settings_changed(self):
        await self.broadcast(self.msg("settings", self.sim.settings))
        await self.broadcast(self.msg("state", self.sim.state()))

    # ---- HTTP --------------------------------------------------------------------
    async def index(self, request):
        return web.FileResponse(DATA_DIR / "index.html", headers={"Cache-Control": "no-cache"})

    async def api_state(self, request):
        return web.json_response(self.sim.state())

    async def api_settings_get(self, request):
        return web.json_response(self.sim.settings)

    async def api_settings_post(self, request):
        try:
            body = await request.json()
        except (json.JSONDecodeError, UnicodeDecodeError):
            return web.json_response({"error": "JSON inválido"}, status=400)
        if not isinstance(body, dict):
            return web.json_response({"error": "se esperaba un objeto"}, status=400)
        changed = [self.sim.apply_set(k, v) for k, v in body.items()]
        if any(changed):
            await self.settings_changed()
        return web.json_response(self.sim.settings, status=200 if all(changed) else 400)

    async def ws(self, request):
        ws = web.WebSocketResponse(heartbeat=20)
        await ws.prepare(request)
        self.clients.add(ws)
        host = request.host.split(":")[0]
        ip = self.ip if host in ("localhost", "127.0.0.1") else host
        try:
            await ws.send_str(self.msg("settings", self.sim.settings))
            await ws.send_str(self.msg("sys", self.sim.sys(ip)))
            await ws.send_str(self.msg("history", self.sim.history()))
            await ws.send_str(self.msg("state", self.sim.state()))
            await ws.send_str(self.msg("frames", self.sim.frames()))
            async for m in ws:
                if m.type != WSMsgType.TEXT:
                    continue
                try:
                    j = json.loads(m.data)
                except json.JSONDecodeError:
                    continue
                if not isinstance(j, dict):
                    continue
                if j.get("t") == "set" and self.sim.apply_set(j.get("k"), j.get("v")):
                    print(f"set {j.get('k')} = {j.get('v')!r}")
                    await self.settings_changed()
                elif j.get("t") == "cmd" and self.sim.apply_cmd(j.get("c")):
                    print(f"cmd {j.get('c')}")
                    if j.get("c") == "clear":
                        await self.broadcast(self.msg("frames", self.sim.frames()))
                    await self.broadcast(self.msg("state", self.sim.state()))
        finally:
            self.clients.discard(ws)
        return ws


@web.middleware
async def no_cache(request, handler):
    resp = await handler(request)
    resp.headers.setdefault("Cache-Control", "no-cache")
    return resp


def main():
    ap = argparse.ArgumentParser(description="Servidor de pruebas MocciaCAN (imita al ESP32)")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--host", default="0.0.0.0")
    args = ap.parse_args()

    ip = local_ip()
    app = App(ip)
    web_app = web.Application(middlewares=[no_cache])
    web_app.router.add_get("/", app.index)
    web_app.router.add_get("/api/state", app.api_state)
    web_app.router.add_get("/api/settings", app.api_settings_get)
    web_app.router.add_post("/api/settings", app.api_settings_post)
    web_app.router.add_get("/ws", app.ws)
    web_app.router.add_static("/", DATA_DIR, show_index=False)

    async def start_loop(_):
        web_app["loop_task"] = asyncio.create_task(app.loop())

    async def stop_loop(_):
        web_app["loop_task"].cancel()
        for ws in list(app.clients):
            await ws.close()

    web_app.on_startup.append(start_loop)
    web_app.on_shutdown.append(stop_loop)
    print(f"MocciaCAN mock: http://localhost:{args.port}  (LAN: http://{ip}:{args.port})")
    web.run_app(web_app, host=args.host, port=args.port, print=None)


if __name__ == "__main__":
    main()
