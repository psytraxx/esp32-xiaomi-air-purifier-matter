#!/usr/bin/env python3
"""Simulate the STM32 of a Mi Air Purifier 3H or 3C on a serial port.

Lets the firmware run on any ESP32 dev board without opening the purifier:
connect a USB-UART adapter to the board's MCU-link pins (GPIO16 = ESP32 RX,
GPIO17 = ESP32 TX, plus GND) and run

    python3 tools/mcu_sim.py --port /dev/ttyUSB1              # a 3H
    python3 tools/mcu_sim.py --port /dev/ttyUSB1 --model mb4  # a 3C

The simulator behaves like the real MCU as far as the protocol goes:
  - it is the master: it polls `get_down` every 200 ms and answers every
    `down ...` command (set_properties → result, get_properties → result with
    values, action → result, MIIO_mcu_version_req → mcu_version);
  - it reports its model and firmware version at start, asks `net` and
    `time` now and then, and sends `properties_changed` when its simulated
    sensors drift or its fan state changes;
  - fan speed follows power/mode/levels like the real purifier, roughly.

Type commands on stdin while it runs to play the user at the device:
  power        toggle power (as the physical button would)
  mode N       0 auto, 1 sleep, 2 favorite, 3 manual (3H only)
  level N      manual fan level 1-3 (3H)
  fav N        favorite level 0-14 (3H) or favorite rpm 300-2200 (3C)
  pm N         set PM2.5 (µg/m³)
  filter N     set filter life (%)
  restore      send `restore` (network reset)
  state        print the simulated state
  quit

This models the protocol from Xiaomi's MIoT serial spec and the property
tables in main/purifier_model.cpp. It is not a capture of the real MCU: timings
and replies may differ from a real unit.

Requires pyserial (`pip install pyserial`).
"""

import argparse
import random
import shlex
import sys
import threading
import time

try:
    import serial  # type: ignore
except ImportError:  # pragma: no cover - only hit without pyserial
    serial = None

# Per-model property tables; mirror main/purifier_model.cpp.
MODELS = {
    "mb3": {
        "name": "zhimi.airpurifier.mb3",
        "props": {
            (2, 1): 0,        # fault
            (2, 2): True,     # power
            (2, 4): 1,        # fan level (manual)
            (2, 5): 0,        # mode: 0 auto, 1 sleep, 2 favorite, 3 manual
            (3, 6): 8,        # PM2.5
            (3, 7): 45,       # humidity %
            (3, 8): 21.5,     # temperature °C
            (4, 3): 87,       # filter life %
            (4, 5): 420,      # filter hours used
            (5, 1): True,     # buzzer
            (6, 1): 0,        # brightness
            (7, 1): False,    # child lock
            (10, 8): 700,     # motor rpm
            (10, 10): 5,      # favorite level 0-14
            (12, 1): 123456,  # use time s
            (13, 1): 9999,    # purified volume
            (13, 2): 10,      # average AQI
            (13, 8): 0,       # AQI state
            (13, 9): 0,       # heartbeat
            (15, 5): False,   # filter door open
        },
        "power": (2, 2), "mode": (2, 5), "level": (2, 4), "fav": (10, 10),
        "rpm": (10, 8), "pm": (3, 6), "aqi": (13, 8), "temp": (3, 8),
        "filter": (4, 3), "filter_hours": (4, 5), "modes": (0, 1, 2, 3),
        "fav_range": (0, 14), "rpm_max": 2300,
    },
    "mb4": {
        "name": "zhimi.airp.mb4a",
        "props": {
            (2, 1): True,     # power
            (2, 2): 0,        # fault
            (2, 4): 0,        # mode: 0 auto, 1 sleep, 2 favorite
            (3, 4): 8,        # PM2.5
            (4, 1): 87,       # filter life %
            (4, 3): 420,      # filter hours used
            (6, 1): True,     # buzzer
            (7, 2): 8,        # brightness 0-8
            (8, 1): False,    # child lock
            (9, 1): 700,      # motor rpm
            (9, 3): 1000,     # favorite rpm 300-2200, steps of 10
            (9, 4): 0,        # heartbeat
        },
        "power": (2, 1), "mode": (2, 4), "level": None, "fav": (9, 3),
        "rpm": (9, 1), "pm": (3, 4), "aqi": None, "temp": None,
        "filter": (4, 1), "filter_hours": (4, 3), "modes": (0, 1, 2),
        "fav_range": (300, 2200), "rpm_max": 2200,
    },
}

MANUAL_RPM = {1: 700, 2: 1300, 3: 2100}


def to_wire(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        return f"{v:g}"
    if isinstance(v, int):
        return str(v)
    return '"' + str(v).replace("\\", "\\\\").replace('"', '\\"') + '"'


def parse_value(tok):
    if tok == "true":
        return True
    if tok == "false":
        return False
    if tok.startswith('"') and tok.endswith('"') and len(tok) >= 2:
        return tok[1:-1]
    try:
        return int(tok)
    except ValueError:
        pass
    try:
        return float(tok)
    except ValueError:
        return tok


class Purifier:
    def __init__(self, model):
        self.m = MODELS[model]
        self.p = dict(self.m["props"])
        self.lock = threading.Lock()
        self.changed = []  # (siid, piid) waiting to be reported

    def k(self, name):
        return self.m[name]

    def set(self, key, value, report=True):
        if key not in self.p:
            return -4003  # property does not exist
        old = self.p[key]
        if isinstance(old, bool) and not isinstance(value, bool):
            return -4005  # wrong type
        if key == self.k("mode") and value not in self.k("modes"):
            return -4005
        if key == self.k("level") and value not in (1, 2, 3):
            return -4005
        lo, hi = self.k("fav_range")
        if key == self.k("fav") and not (lo <= value <= hi):
            return -4005
        self.p[key] = value
        self._follow_fan()
        if report and old != value:
            self.changed.append(key)
        return 0

    def _follow_fan(self):
        on, mode = self.p[self.k("power")], self.p[self.k("mode")]
        fav = self.p[self.k("fav")]
        if not on:
            rpm = 0
        elif mode == 1:
            rpm = 400
        elif mode == 2:
            rpm = fav if self.k("level") is None else 300 + fav * 2000 // 14
        elif mode == 3:
            rpm = MANUAL_RPM[self.p[self.k("level")]]
        else:
            rpm = 500 + self.p[self.k("pm")] * 15
        rpm = min(rpm, self.k("rpm_max"))
        if rpm != self.p[self.k("rpm")]:
            self.p[self.k("rpm")] = rpm
            self.changed.append(self.k("rpm"))

    def reset_filter(self):
        self.p[self.k("filter")] = 100
        self.p[self.k("filter_hours")] = 0
        self.changed += [self.k("filter"), self.k("filter_hours")]

    def drift(self):
        pm_key = self.k("pm")
        pm = max(0, self.p[pm_key] + random.choice((-2, -1, 0, 0, 1, 2)))
        self.p[pm_key] = pm
        self.changed.append(pm_key)
        if self.k("aqi"):
            self.p[self.k("aqi")] = 0 if pm < 13 else 1 if pm < 36 else 2 if pm < 56 else 3 if pm < 151 else 4 if pm < 251 else 5
            self.changed.append(self.k("aqi"))
        if self.k("temp"):
            self.p[self.k("temp")] = round(self.p[self.k("temp")] + random.choice((-0.1, 0, 0.1)), 1)
            self.changed.append(self.k("temp"))
        self._follow_fan()


class Simulator:
    def __init__(self, port, verbose, model, announce=True):
        self.port = port
        self.verbose = verbose
        self.dev = Purifier(model)
        self.announce = announce
        self.out_queue = []  # MCU-initiated commands waiting to be sent
        self.running = True

    def log(self, direction, line):
        if self.verbose or line not in ("get_down", "down none"):
            print(f"{time.strftime('%H:%M:%S')} {direction} {line}", flush=True)

    def transact(self, line):
        """Send one line and wait for the module's one-line reply."""
        self.log("MCU ->", line)
        self.port.write((line + "\r").encode())
        buf = b""
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            b = self.port.read(1)
            if not b:
                continue
            if b == b"\r":
                reply = buf.decode(errors="replace")
                self.log("ESP <-", reply)
                return reply
            if b != b"\n":
                buf += b
        print("!! no reply from module within 1 s", flush=True)
        return None

    def handle_down(self, reply):
        toks = shlex.split(reply, posix=False)
        if len(toks) < 2 or toks[0] != "down" or toks[1] == "none":
            return
        cmd, args = toks[1], toks[2:]
        with self.dev.lock:
            if cmd == "set_properties":
                parts = []
                for i in range(0, len(args) - 2, 3):
                    key = (int(args[i]), int(args[i + 1]))
                    code = self.dev.set(key, parse_value(args[i + 2]), report=False)
                    parts.append(f"{key[0]} {key[1]} {code}")
                self.out_queue.insert(0, "result " + " ".join(parts))
            elif cmd == "get_properties":
                parts = []
                for i in range(0, len(args) - 1, 2):
                    key = (int(args[i]), int(args[i + 1]))
                    if key in self.dev.p:
                        parts.append(f"{key[0]} {key[1]} 0 {to_wire(self.dev.p[key])}")
                    else:
                        parts.append(f"{key[0]} {key[1]} -4003")
                self.out_queue.insert(0, "result " + " ".join(parts))
            elif cmd == "action":
                siid, aiid = int(args[0]), int(args[1])
                if (siid, aiid) == (4, 1):
                    self.dev.reset_filter()
                self.out_queue.insert(0, f"result {siid} {aiid} 0")
            elif cmd == "MIIO_mcu_version_req":
                self.out_queue.insert(0, "mcu_version 0001")
            elif cmd == "MIIO_net_change":
                print(f"** Wi-Fi LED now shows: {args[0] if args else '?'}", flush=True)

    def run(self):
        threading.Thread(target=self.console, daemon=True).start()
        if self.announce:
            self.transact("model " + self.dev.m["name"])
        self.transact("mcu_version 0001")
        last_net = last_time = last_drift = 0.0
        while self.running:
            now = time.monotonic()
            with self.dev.lock:
                if now - last_drift > 10:
                    last_drift = now
                    self.dev.drift()
                if self.dev.changed:
                    keys = list(dict.fromkeys(self.dev.changed))
                    self.dev.changed.clear()
                    body = " ".join(f"{k[0]} {k[1]} {to_wire(self.dev.p[k])}" for k in keys)
                    self.out_queue.append("properties_changed " + body)
            if now - last_net > 5:
                last_net = now
                self.out_queue.append("net")
            if now - last_time > 60:
                last_time = now
                self.out_queue.append("time")

            if self.out_queue:
                line = self.out_queue.pop(0)
                self.transact(line)
            else:
                reply = self.transact("get_down")
                if reply:
                    self.handle_down(reply)
            time.sleep(0.2)

    def console(self):
        for raw in sys.stdin:
            words = raw.split()
            if not words:
                continue
            cmd, arg = words[0], (int(words[1]) if len(words) > 1 and words[1].lstrip("-").isdigit() else None)
            with self.dev.lock:
                d = self.dev
                if cmd == "power":
                    d.set(d.k("power"), not d.p[d.k("power")])
                elif cmd == "mode" and arg is not None:
                    print("rejected" if d.set(d.k("mode"), arg) else "ok")
                elif cmd == "level" and arg is not None and d.k("level"):
                    print("rejected" if d.set(d.k("level"), arg) else "ok")
                elif cmd == "fav" and arg is not None:
                    print("rejected" if d.set(d.k("fav"), arg) else "ok")
                elif cmd == "pm" and arg is not None:
                    d.set(d.k("pm"), arg)
                elif cmd == "filter" and arg is not None:
                    d.set(d.k("filter"), arg)
                elif cmd == "restore":
                    self.out_queue.append("restore")
                elif cmd == "state":
                    for k, v in sorted(self.dev.p.items()):
                        print(f"  {k[0]}.{k[1]} = {to_wire(v)}")
                elif cmd == "quit":
                    self.running = False
                    return
                else:
                    print("commands: power | mode N | level N | fav N | pm N | filter N | restore | state | quit")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", required=True, help="serial port wired to the ESP32's MCU-link pins")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--model", choices=sorted(MODELS), default="mb3", help="mb3 = 3H (default), mb4 = 3C")
    ap.add_argument("--silent-model", action="store_true",
                    help="don't announce the model at start, as when the ESP32 restarts on its own; "
                         "the firmware then has to tell the model from its probe")
    ap.add_argument("-v", "--verbose", action="store_true", help="also log idle get_down / down none")
    args = ap.parse_args()
    if serial is None:
        sys.exit("pyserial is required: pip install pyserial")
    port = serial.Serial(args.port, args.baud, timeout=0.05)
    try:
        Simulator(port, args.verbose, args.model, not args.silent_model).run()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
