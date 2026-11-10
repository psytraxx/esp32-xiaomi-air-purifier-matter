# esp32-xiaomi-air-purifier-matter

Cloud-free [Matter](https://csa-iot.org/all-solutions/matter/) firmware for the
**Xiaomi Mi Air Purifier 3H** (`zhimi.airpurifier.mb3`, also sold as Mi Air
Purifier 3).

The firmware replaces the stock firmware on the purifier's built-in ESP32
Wi-Fi module. The ESP32 then joins your Matter fabric directly (Apple Home,
Google Home, Home Assistant, SmartThings, …) instead of talking to the Xiaomi
cloud. No extra hardware or soldering is required.

> **Status: early development / planning.** Nothing here is ready to flash
> yet. This README documents the hardware, protocol and design the firmware
> is being built against.

---

## Contents

- [How it works](#how-it-works)
- [Supported devices](#supported-devices)
- [Hardware](#hardware)
- [MCU ↔ ESP32 serial protocol](#mcu--esp32-serial-protocol)
- [Matter data model](#matter-data-model)
- [Flashing](#flashing)
- [Building](#building)
- [Roadmap](#roadmap)
- [Credits and prior work](#credits-and-prior-work)
- [Disclaimer](#disclaimer)

---

## How it works

The 3H contains two microcontrollers:

```
                    ┌──────────────────────────────────────────┐
                    │           Mi Air Purifier 3H             │
                    │                                          │
 Matter controller  │  ┌──────────────┐  UART   ┌───────────┐  │
 (Wi-Fi, BLE for ◄──┼─►│ ESP32        │ 115200  │ STM32F412 │──┼─► fan motor, PM2.5,
  commissioning)    │  │ (this fw)    │◄───────►│ (stock fw)│  │   temp/humidity,
                    │  │ ESP-WROOM-32D│  8N1    │           │  │   display, buttons,
                    │  └──────────────┘         └───────────┘  │   buzzer, filter RFID
                    └──────────────────────────────────────────┘
```

- The **STM32F412** runs Xiaomi's stock firmware and does all the real work:
  motor control, sensors, display, buttons, buzzer and filter RFID. We do
  not touch it.
- The **ESP32** is only a network gateway. Stock, it bridges the MCU to the
  Xiaomi cloud using the text-based *MIoT serial protocol*. This project
  replaces that firmware with one that speaks the same serial protocol to
  the MCU and exposes the device as a Matter Air Purifier.

Because the MCU keeps its stock firmware, fan control, safety logic and the
physical buttons behave exactly as before.

## Supported devices

| Device | Model ID | Status |
|---|---|---|
| Mi Air Purifier 3 / 3H | `zhimi.airpurifier.mb3` | Target, in development |

Other MIoT purifiers with an ESP32 gateway and a separate MCU (3C, 4,
4 Lite, Pro H, …) use the same protocol with different property IDs, and
could be added later. The following are **not** supported:

- Devices with a proprietary Xiaomi Wi-Fi chip, e.g. 2S/2H (`MHCW02P`).
- Devices using the legacy miIO protocol, e.g. the original Air Purifier 2.

See the [esphome-miot unsupported list](https://github.com/dhewg/esphome-miot#unsupported-devices)
for details.

## Hardware

| Item | Detail |
|---|---|
| Gateway module | Espressif **ESP-WROOM-32D** (classic ESP32, Wi-Fi + BLE 4.2, no 802.15.4) |
| Main MCU | **STM32F412** |
| MCU link | ESP32 `GPIO17` = TX, `GPIO16` = RX, 115200 baud, 8N1 |
| Flash header | `3.3V`, `GND`, `TX`, `RX`, with a `GPIO0` / `GND` pin pair right above it |
| MAC address | Xiaomi writes a custom MAC into eFuse. Builds must ignore the custom-MAC / CRC eFuse checks (`ignore_efuse_custom_mac`, `ignore_efuse_mac_crc` in esphome-miot). |
| Cores | esphome-miot builds with `CONFIG_FREERTOS_UNICORE=y`. Confirm single- vs dual-core on your unit with `esptool.py chip_id`. |
| Flash size | Expected 4 MB. **To verify** with `esptool.py flash_id` before choosing a partition table. |

Disassembly: see the
[esphome-miot 3H wiki](https://github.com/dhewg/esphome-miot/wiki/Xiaomi-Mi-Air-Purifier-3H)
and the linked disassembly video.

## MCU ↔ ESP32 serial protocol

The protocol is Xiaomi's *MIoT serial communication*. Here is a translated
copy of the [official spec](https://github.com/blakadder/miot).

- ASCII text commands; tokens are separated by spaces and each line ends with `\r` (0x0D).
- Maximum line length is 512 bytes.
- **The MCU is the master.** It polls the module with `get_down` every
  100–200 ms. The module answers each line it receives.

Typical exchange (`↑` MCU → ESP32, `↓` ESP32 → MCU):

```
↑ get_down
↓ down none                              # nothing queued

↑ get_down
↓ down set_properties 2 2 true           # e.g. user turned power on via Matter
↑ result 2 2 0                           # siid piid code (0 = success)
↓ ok

↑ properties_changed 3 6 12              # PM2.5 changed to 12 µg/m³
↓ ok

↑ net
↓ cloud                                  # one of: offline | unprov | uap | local | cloud
```

Commands the module must handle:

| From MCU | Meaning | Reply |
|---|---|---|
| `get_down` | Poll for a queued command | `down none` or `down <get_properties\|set_properties\|action> …` |
| `properties_changed <siid> <piid> <value> …` | Property report | `ok` |
| `event_occured <siid> <eiid> …` | Event report | `ok` |
| `result <siid> <piid> <code> [value] …` | Result of a queued get/set/action | `ok` |
| `net` | Ask for network state (drives the Wi-Fi LED) | `offline` / `unprov` / `uap` / `local` / `cloud` |
| `time [posix]` | Ask for current time | Time string |
| `mac` | Ask for MAC | MAC |
| `model <model>` | Report model | `ok` |
| `mcu_version <nnnn>` | Report MCU firmware version | `ok` |
| `error "<msg>" <code>` | MCU-side error | `ok` |
| `restore` | Factory reset request | `ok` |
| anything else | Unknown | `error` |

esphome-miot also writes a periodic **heartbeat** to property `13.9`. We
replicate that.

[`components/miot/miot.cpp` in esphome-miot](https://github.com/dhewg/esphome-miot/blob/main/components/miot/miot.cpp)
is a complete, field-tested reference implementation.

## Matter data model

Property IDs come from the
[MIoT spec for `zhimi.airpurifier.mb3`](https://home.miot-spec.com/spec/zhimi.airpurifier.mb3)
and the
[esphome-miot 3H config](https://github.com/dhewg/esphome-miot/blob/main/config/zhimi.airpurifier.mb3.yaml).

### Endpoints

| Endpoint | Matter device type | Clusters |
|---|---|---|
| 0 | Root Node | Basic Information, Network Commissioning, … |
| 1 | **Air Purifier** (`0x002D`) | Identify, On/Off, **Fan Control**, **HEPA Filter Monitoring** |
| 2 | **Air Quality Sensor** (`0x002C`) | Air Quality, PM2.5 Concentration Measurement |
| 3 | Temperature Sensor (`0x0302`) | Temperature Measurement |
| 4 | Humidity Sensor (`0x0307`) | Relative Humidity Measurement |

### Property mapping

| MIoT (siid.piid) | Description | Values | Matter |
|---|---|---|---|
| 2.2 | Power | bool | On/Off `OnOff`; Fan Control `FanMode` = Off |
| 2.5 | Mode | 0 Auto, 1 Sleep, 2 Favorite, 3 Manual | Fan Control `FanMode` Auto / Low (sleep) / … |
| 2.4 | Manual fan level | 1–3 | Fan Control `FanMode` Low / Medium / High |
| 10.10 | Favorite level | 0–14 | Fan Control `SpeedSetting` (`SpeedMax` = 14), `PercentSetting` |
| 10.8 | Motor speed | 300–2300 rpm | Fan Control `SpeedCurrent` / `PercentCurrent` |
| 3.6 | PM2.5 | µg/m³ | PM2.5 Concentration Measurement `MeasuredValue` |
| 13.8 | AQI state | 0 Best … 5 Unhealthy | Air Quality `AirQuality` |
| 3.8 | Temperature | °C (float) | Temperature Measurement (0.01 °C units) |
| 3.7 | Relative humidity | % | Relative Humidity Measurement (0.01 % units) |
| 4.3 | Filter life | % | HEPA Filter Monitoring `Condition` |
| 4.5 | Filter used time | h | HEPA Filter Monitoring `LastChangedTime` (derived) |
| action 4.1 `3 0` | Reset filter | — | HEPA Filter Monitoring `ResetCondition` command |
| action 8.1 | Toggle power | — | (physical button equivalent, not exposed) |
| 2.1 | Fault code | 0 none, 1 M1 run, 2 M1 stuck, 3 no sensor, 4 humidity err, 5 temp err | Logged; no standard cluster |
| 5.1 | Buzzer | bool | Not exposed (no standard cluster) |
| 6.1 | Display brightness | 0 High, 1 Low, 2 Off | Not exposed (no standard cluster) |
| 7.1 | Child lock | bool | Not exposed (no standard cluster) |
| 15.5 | Filter door open | bool | Logged |

Properties without a standard Matter cluster could later be exposed as
vendor-specific attributes, but most controllers ignore those.

## Flashing

> ⚠️ **Back up the original flash first.** The backup is the only way back to
> stock firmware and Mi Home. It contains device-specific secrets (DID, keys,
> token): keep it private and don't publish it.

1. Unplug the purifier from mains and open it (see [Hardware](#hardware)).
2. Connect a 3.3 V USB-UART adapter to the header: `3.3V`, `GND`, `TX`→`RX`,
   `RX`→`TX`.
3. Short the `GPIO0` and `GND` pins to enter the ROM bootloader.
4. Power the board **from the UART adapter only**. Never connect mains at
   the same time.
5. Identify the chip and back up the flash:

   ```bash
   esptool.py --port /dev/ttyUSB0 chip_id
   esptool.py --port /dev/ttyUSB0 flash_id
   esptool.py --port /dev/ttyUSB0 read_flash 0 ALL stock_backup.bin
   ```

6. Flash this firmware (instructions follow once there's a release).
7. Remove the `GPIO0`–`GND` short, power-cycle, and commission the device
   with the QR code / manual pairing code printed on the serial console.

To restore stock firmware:

```bash
esptool.py --port /dev/ttyUSB0 write_flash 0 stock_backup.bin
```

## Building

Planned toolchain:

- [ESP-IDF](https://github.com/espressif/esp-idf) (the version required by the
  chosen esp-matter release)
- [esp-matter](https://github.com/espressif/esp-matter) (Espressif's Matter SDK,
  based on connectedhomeip)

Target `esp32`; Matter over Wi-Fi, commissioned over BLE. Build instructions
will be added with the first firmware commit.

## Roadmap

- [ ] Confirm chip variant (single/dual core) and flash size on real hardware
- [ ] Capture a stock MCU↔ESP32 UART log (boot, button presses, app commands)
- [ ] esp-matter project skeleton for `esp32` with a 4 MB partition table (with OTA if it fits)
- [ ] MIoT UART driver: line framing, `get_down` command queue, `result`/`properties_changed` parsing
- [ ] `net` state reporting tied to Matter/Wi-Fi state (correct LED behaviour)
- [ ] Air Purifier endpoint: On/Off + Fan Control
- [ ] HEPA Filter Monitoring incl. reset
- [ ] Air Quality, PM2.5, temperature, humidity endpoints
- [ ] Factory reset via button combo / `restore`
- [ ] Matter OTA updates
- [ ] Tested with Apple Home, Google Home and Home Assistant
- [ ] Possibly support other MIoT purifiers (3C, 4, 4 Lite, …) through per-model property tables

## Credits and prior work

This project builds on the work of others:

- [dhewg/esphome-miot](https://github.com/dhewg/esphome-miot): ESPHome MIoT
  components and the 3H config/wiki that this project's protocol handling
  and property map are based on
- [jaromeyer/mipurifier-esphome](https://github.com/jaromeyer/mipurifier-esphome):
  the original ESPHome firmware for the 3H
- [sourcediver.org: Unclouded purifier](https://sourcediver.org/posts/240218_unclouded_purifier/):
  the write-up that inspired this project
- [blakadder/miot](https://github.com/blakadder/miot): English translation of
  Xiaomi's MIoT serial protocol docs
- [Flamingo-tech/xiaomi-air-purifier-reverse-engineering](https://github.com/Flamingo-tech/xiaomi-air-purifier-reverse-engineering):
  3H hardware reverse engineering and dumps
- [muwave.de: Improving a Xiaomi Air Purifier 3H](https://blog.muwave.de/2025/02/hacking-the-xiaomi-air-purifier-3h-1-3/)

## Disclaimer

This is an unofficial project, not affiliated with or endorsed by Xiaomi,
Zhimi, Espressif or the Connectivity Standards Alliance. Opening the device
voids its warranty and exposes mains-powered parts, and flashing can brick
the Wi-Fi module. You do this at your own risk.

## License

TBD.
