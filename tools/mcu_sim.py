#!/usr/bin/env python3
"""Simulate the Mi Air Purifier 3H's STM32 on a serial port.

Lets the firmware run on any ESP32 dev board without opening the purifier:
connect a USB-UART adapter to the board's MCU-link pins (GPIO16 = ESP32 RX,
GPIO17 = ESP32 TX, plus GND) and run

    python3 tools/mcu_sim.py --port /dev/ttyUSB1

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
  mode N       0 auto, 1 sleep, 2 favorite, 3 manual
  level N      manual fan level 1-3
  fav N        favorite level 0-14
  pm N         set PM2.5 (µg/m³)
  filter N     set filter life (%)
  restore      send `restore` (network reset)
  state        print the simulated state
  quit

This models the protocol from Xiaomi's MIoT serial spec and the 3H property
table in main/purifier_model.h. It is not a capture of the real MCU — replace
the timings and replies with what the Phase 0 capture shows where they differ.

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

# (siid, piid) → initial value; mirrors main/purifier_model.h.
PROPS = {
    (2, 1): 0,        # fault
    (2, 2): True,     # power
    (2, 4): 1,        # fan level (manual)
    (2, 5): 0,        # mode
    (3, 6): 8,        # PM2.5
    (3, 7): 45,       # humidity %
    (3, 8): 21.5,     # temperature °C
    (4, 3): 87,       # filter life %
    (4, 5): 420,      # filter hours used
    (5, 1): True,     # buzzer
    (6, 1): 0,        # brightness
    (7, 1): False,    # child lock
    (10, 8): 700,     # motor rpm
    (10, 10): 5,      # favorite level
    (12, 1): 123456,  # use time s
    (13, 1): 9999,    # purified volume
    (13, 2): 10,      # average AQI
    (13, 8): 0,       # AQI state
    (13, 9): 0,       # heartbeat
    (15, 5): False,   # filter door open
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
    def __init__(self):
        self.p = dict(PROPS)
        self.lock = threading.Lock()
        self.changed = []  # (siid, piid) waiting to be reported

    def set(self, key, value, report=True):
        if key not in self.p:
            return -4003  # property does not exist
        old = self.p[key]
        if isinstance(old, bool) and not isinstance(value, bool):
            return -4005  # wrong type
        if key == (2, 5) and value not in (0, 1, 2, 3):
            return -4005
        if key == (2, 4) and value not in (1, 2, 3):
            return -4005
        if key == (10, 10) and not (0 <= value <= 14):
            return -4005
        self.p[key] = value
        self._follow_fan()
        if report and old != value:
            self.changed.append(key)
        return 0

    def _follow_fan(self):
        on, mode = self.p[(2, 2)], self.p[(2, 5)]
        if not on:
            rpm = 0
        elif mode == 1:
            rpm = 400
        elif mode == 2:
            rpm = 300 + self.p[(10, 10)] * 2000 // 14
        elif mode == 3:
            rpm = MANUAL_RPM[self.p[(2, 4)]]
        else:
            rpm = 500 + self.p[(3, 6)] * 15
        rpm = min(rpm, 2300)
        if rpm != self.p[(10, 8)]:
            self.p[(10, 8)] = rpm
            self.changed.append((10, 8))

    def drift(self):
        pm = max(0, self.p[(3, 6)] + random.choice((-2, -1, 0, 0, 1, 2)))
        self.p[(3, 6)] = pm
        self.p[(13, 8)] = 0 if pm < 13 else 1 if pm < 36 else 2 if pm < 56 else 3 if pm < 151 else 4 if pm < 251 else 5
        self.p[(3, 8)] = round(self.p[(3, 8)] + random.choice((-0.1, 0, 0.1)), 1)
        self.changed += [(3, 6), (13, 8), (3, 8)]
        self._follow_fan()


class Simulator:
    def __init__(self, port, verbose):
        self.port = port
        self.verbose = verbose
        self.dev = Purifier()
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
                    self.dev.p[(4, 3)] = 100
                    self.dev.p[(4, 5)] = 0
                    self.dev.changed += [(4, 3), (4, 5)]
                self.out_queue.insert(0, f"result {siid} {aiid} 0")
            elif cmd == "MIIO_mcu_version_req":
                self.out_queue.insert(0, "mcu_version 0001")
            elif cmd == "MIIO_net_change":
                print(f"** Wi-Fi LED now shows: {args[0] if args else '?'}", flush=True)

    def run(self):
        threading.Thread(target=self.console, daemon=True).start()
        self.transact("model zhimi.airpurifier.mb3")
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
                if cmd == "power":
                    self.dev.set((2, 2), not self.dev.p[(2, 2)])
                elif cmd == "mode" and arg is not None:
                    self.dev.set((2, 5), arg)
                elif cmd == "level" and arg is not None:
                    self.dev.set((2, 4), arg)
                elif cmd == "fav" and arg is not None:
                    self.dev.set((10, 10), arg)
                elif cmd == "pm" and arg is not None:
                    self.dev.set((3, 6), arg)
                elif cmd == "filter" and arg is not None:
                    self.dev.set((4, 3), arg)
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
    ap.add_argument("-v", "--verbose", action="store_true", help="also log idle get_down / down none")
    args = ap.parse_args()
    if serial is None:
        sys.exit("pyserial is required: pip install pyserial")
    port = serial.Serial(args.port, args.baud, timeout=0.05)
    try:
        Simulator(port, args.verbose).run()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
