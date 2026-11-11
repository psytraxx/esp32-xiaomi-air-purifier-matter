# Changelog

Entries are grouped by date, newest first.

## 2026-11-11

### Added
- First run on a real 3H: the firmware talks to the purifier's controller, recognises the 3H on its own and reads power, mode, PM2.5, temperature, humidity and filter life. The controller also reports changes by itself, so the Matter values follow the purifier without waiting for the next check.
- A debug build option that prints every message exchanged with the purifier's controller, for bringing up new units and checking the protocol.
- A WPA3 version of the firmware can be built to try on mixed WPA2/WPA3 networks. Tested on a FRITZ!Box 4040 in mixed mode it still couldn't connect, so the normal build stays WPA2-only; the WPA3 version is only worth trying with other routers.
- When Wi-Fi fails to connect, the console now says why (for example a wrong password, a security setting the purifier can't use, or a weak signal), with a hint on what to change on the router. Once connected it shows which network and security mode it joined.

### Fixed
- The purifier now joins mixed WPA2/WPA3 Wi-Fi networks: confirmed on a 3H with a FRITZ!Box, where it connected at once and paired with Home Assistant. It used to drop out during the Wi-Fi password exchange; it now always connects with WPA2. Networks that only allow WPA3 aren't supported.

---

## 2026-11-10

### Added
- Hardware facts from the first 3H backup are recorded (dump/README.md): the Wi-Fi module is single-core with 4 MB of flash, and its built-in network address has a broken checksum. The firmware's settings already assumed all three, so nothing had to change.
- A fault reported by the purifier (such as a stuck fan or a broken sensor) now shows up in Matter as a hardware fault. In Home Assistant it appears as the "Hardware faults" problem sensor, which has to be enabled once on the device page.
- The firmware now also runs the Mi Air Purifier 3C. The same image works in either purifier: it recognises which one it is in, remembers it, and offers the matching features (the 3C has no temperature or humidity sensor). On the 3C, low, medium and high are set fan speeds, and its air-quality level is worked out from the PM2.5 reading. Not yet tried on a real 3C.
- The purifier simulator can play a 3C as well, and can stay quiet about its model to test the automatic recognition.
- The README links the esphome-miot wiki pages for the 3H and the 3C, which have board photos and flashing notes.
- First firmware for the purifier's Wi-Fi module: it talks to the purifier's own controller the way Xiaomi's firmware did, and offers the purifier to Matter as an air purifier with power, auto/low/medium/high modes, a 15-step fine speed, sleep mode, filter life with reset, air quality, PM2.5, temperature and humidity. Not yet tried on a real purifier.
- The purifier's Wi-Fi light now reflects the Matter state: waiting for pairing, connected, or offline.
- Telling the purifier itself to reset its network now wipes the Matter pairing, so it can be paired again from scratch.
- Automatic checks on every change: the firmware is built for the ESP32, and tests cover how messages to and from the purifier are read and written and how every setting is translated to and from Matter. Each build also produces a ready-to-flash image.
- A guide for working on the code (CLAUDE.md), and the README now explains how to flash the ready-made image and how to test with the simulator.
- A purifier simulator (tools/mcu_sim.py) that stands in for the purifier's controller, so the firmware can be tried on any ESP32 board without opening the purifier.
- An implementation plan (docs/PLAN.md) laying out the steps from a stock purifier to a working Matter one, the decisions made so far, and a table showing which Matter feature each purifier function will appear as.
- Step-by-step guide to backing up the purifier's original Wi-Fi firmware before flashing, checking that the backup is good, and putting it back if anything goes wrong, with a troubleshooting table.
- Project README describing how the purifier works inside, how its two chips talk to each other, how its settings and sensors will appear in Matter, and how to back up and flash the Wi-Fi module.
- The README now records the build setup, which matches the plant monitor project (ESP-IDF 6.1 and Espressif's Matter SDK), and how this purifier differs from it: Wi-Fi instead of Thread, always on instead of sleeping, and a few quirks of Xiaomi's Wi-Fi module.

### Changed
- The backup guide now keeps the chip in flashing mode between commands, recommends a separate 3.3 V power supply, and covers the two connection problems met during the first backup.
- Flashing now starts by erasing the whole chip, so none of Xiaomi's Wi-Fi settings or device identity stay behind in the module's memory.
- Power, modes, fan speed and sleep mode will all be controlled through Matter's standard fan controls, rather than a separate on/off switch. Child lock, beeper and display brightness won't be exposed and stay controllable on the purifier itself.
- The README's diagrams are now drawn with Mermaid, so GitHub renders them as real diagrams.
- The README now makes clear that this firmware is written from scratch on ESP-IDF and doesn't use ESPHome. The ESPHome projects are credited only as prior research into the hardware, and setting IDs now point to Xiaomi's own published spec.
