# CLAUDE.md

Agent-facing guide for working on this firmware. For what the project *is*, the
hardware, the serial protocol, backup/restore and the Matter mapping, read
[README.md](README.md) and [docs/PLAN.md](docs/PLAN.md) — this file does not
repeat them and focuses on the codebase, internal constants and platform
footguns.

Matter firmware for the **Xiaomi Mi Air Purifier 3H** (`zhimi.airpurifier.mb3`)
and **3C** (`zhimi.airp.mb4a` / `zhimi.airpurifier.mb4`) — one image for both,
running on the purifier's own **ESP-WROOM-32D** (classic ESP32, Xtensa, 4 MB
flash). The STM32 main controller keeps its stock firmware; this firmware
replaces Xiaomi's cloud gateway on the ESP32, talks the **MIoT serial protocol**
to the STM32 over UART, and exposes the purifier as a Matter **Air Purifier
over Wi-Fi** (BLE for commissioning only). Mains powered, always on: no ICD, no
sleep. Language: C++ on **native ESP-IDF v6.1** with `espressif/esp_matter`
1.6.1 — the same stack as
[esp32-homecontrol-matter](https://github.com/psytraxx/esp32-homecontrol-matter).

## Required Workflow

Before committing any change:
1. **Build** — `idf.py build` — fix all errors and warnings. CI compiles with
   `-Werror` (from the `espressif/esp-idf-ci-action` image); a local build only
   warns, so a clean local build can still fail CI.
2. **Run the native tests** — `cmake -S test -B test/build && cmake --build test/build && ./test/build/test_logic` — CI runs these as a blocking job. The host build uses `-Wall -Wextra -Werror`.
3. **Update `CHANGELOG.md`** — add an entry under today's date (see style below).

Never skip these steps. If there is no ESP-IDF toolchain where you work, say so
and let the PR's CI build be the check — never claim a firmware build you did
not run.

### CI/CD

[.github/workflows/build.yaml](.github/workflows/build.yaml) runs on every PR
and on pushes to `main`:

| Job | What it does |
|-----|--------------|
| `build` | `idf.py build && idf.py size` for `esp32` via `espressif/esp-idf-ci-action` (`esp_idf_version: v6.1`), then merges a flashable image and uploads the `firmware-esp32` artifact |
| `test` | Host-native Unity tests — no ESP-IDF needed |

- **The IDF version is pinned in two places** — `esp_idf_version:` and the
  `idf:` range in [main/idf_component.yml](main/idf_component.yml). Bump both
  together and update the ccache `key:`.
- `managed_components/` is cached on `dependencies.lock` + `main/idf_component.yml`.
  `dependencies.lock` is committed; commit it again whenever a build updates it.

### Writing CHANGELOG entries

Same rules as the plant monitor: chronological by date, newest first, no
`[Unreleased]`; `## YYYY-MM-DD` heading with `### Added` / `### Changed` /
`### Fixed` / `### Removed`; date sections separated by `---`. Plain language
a non-engineer can follow — what changed and why it matters, not register
names, API calls or issue numbers.

## Build Commands

```bash
source ~/.espressif/v6.1/esp-idf/export.sh   # or, with an EIM install: source ~/.espressif/tools/activate_idf_v6.1.sh
idf.py set-target esp32                 # once per fresh checkout
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor    # module in bootloader mode (GPIO0–GND)
cmake -S test -B test/build && cmake --build test/build && ./test/build/test_logic
python3 tools/mcu_sim.py --port /dev/ttyUSB1   # STM32 stand-in on a devkit
idf.py -B build-debug -DPURIFIER_LINK_DEBUG=1 build   # log every MCU line (bring-up)
curl --data-binary @build/air_purifier_matter.bin http://<purifier>/ota   # update over Wi-Fi (+ -H "Authorization: Bearer <token>")
```

With an EIM install, `export.sh` leaves the Xtensa compiler off the path
("xtensa-esp32-elf-gcc is not a full path"); use the activation script.
`idf.py` is then a shell function, so it can't be wrapped in `timeout`.

Entry point is `app_main()` in [main/app_main.cpp](main/app_main.cpp).

## Code Organisation

| File | Purpose | Host-tested |
|------|---------|-------------|
| `main/miot_protocol.h/.cpp` | MIoT values (`Value`), tokenizer, ID parsing | yes |
| `main/miot_queue.h/.cpp` | Commands waiting for the MCU's `get_down`: FIFO, set-merge, get-dedupe, bounded | yes |
| `main/miot_session.h/.cpp` | One MCU line in → one reply out; hooks for properties, set failures, events, `restore`, `net`, `time`, `mac` | yes |
| `main/purifier_model.h/.cpp` | Per-model `Profile`s (3H, 3C), model detection/decision rules, tracked `State`, all MIoT ↔ Matter conversions (the tables in docs/PLAN.md) | yes |
| `main/wifi_diag.h/.cpp` | Wi-Fi disconnect reason / security-mode names (ESP-IDF v6.1 values), hints, repeat filter | yes |
| `main/ota_policy.h/.cpp` | Keep/roll-back decision for a new image, update-token check | yes |
| `main/ota_server.h/.cpp` | HTTP update server (`GET /`, `POST /ota`), image verification after an update | no |
| `main/wifi_events.h/.cpp` | Logs station connects/disconnects via `wifi_diag` (the CHIP SDK log level hides them); registered after `esp_matter::start()` | no |
| `main/purifier_link.h/.cpp` | UART2 driver + link task: line framing, session under a recursive mutex, 30 s poll, 60 s heartbeat, MCU-silence detection | no |
| `main/matter_setup.h/.cpp` | Node + endpoints, Fan Control / HEPA delegates, state → cluster refresh on the CHIP task, commissioning + Wi-Fi events, SNTP | no |
| `main/app_main.cpp` | Boot order (boot_diag → link → model choice → Matter), remembered model in NVS, restart on a model change, `restore` timer, heap log loop | no |
| `main/boot_diag.h/.cpp` | Reset reason, boot streak, Matter-start fault counter → wipe Matter NVS (from the plant monitor) | no |
| `main/app_config.h` | Tunable constants | — |
| `main/board_pins.h` | UART number and pins | — |
| `main/chip_project_config.h` | Pairing identity, Basic Information strings | — |
| `tools/mcu_sim.py` | STM32 simulator for a devkit (`--model mb3`/`mb4`, `--silent-model`) | — |

Keep protocol and mapping logic in the host-tested files; the ESP-only files
should stay thin glue.

## Supported Models

Everything model-specific lives in a `purifier::Profile` in
[main/purifier_model.cpp](main/purifier_model.cpp): property IDs (optional
ones absent on a model), mode wire values, fan scheme (3H: manual levels +
favorite level 0–14; 3C: favorite rpm only), rpm ranges, 3C Low/Medium/High
rpm presets, heartbeat, reset-filter action. Code outside `purifier_model`
must not hard-code a property ID — go through the profile. To add a model,
add a profile, extend `model_from_string`, pick a probe property only it
has, and add round-trip tests.

Model choice (`app_main.cpp`, rules in `boot_model()` / `on_model_detected()`):

1. **Forced** by `-DPURIFIER_MODEL=1|2` (0 = detect, the default in
   `app_config.h`): used as is; a mismatching detection only logs.
2. Else the model **remembered in NVS** (`purifier`/`model`) from an earlier
   boot.
3. Else **detect now**: the link queues a probe (`get_properties 10 8 9 1` —
   only the 3H answers 10.8, only the 3C 9.1) and also listens for the MCU's
   `model` announcement, which it only sends when it boots. Waits up to
   `MODEL_DETECT_TIMEOUT_MS`, then falls back to the 3H without remembering.

A later detection that differs from the running model stores it and
**restarts**, because the Matter endpoints depend on the model. The link
polls nothing but the probe until `purifier_link_set_model()` has chosen a
profile.

## Data Flow

```
MCU line ─► purifier_link task ─► Session::handle_line ─► reply ─► MCU
                                     │ on_properties
                                     ▼
                               purifier::apply(State) ─► changed bits
                                     │ matter_on_purifier_changed (atomic OR, one ScheduleWork)
                                     ▼
                     CHIP task: refresh_pending() ─► cluster setters

Controller write ─► FanControlCluster ─► PurifierFanDelegate::OnFanDriveStateChanged
                                     │ (ignored while s_applying_purifier_state)
                                     ▼
                  purifier_link_apply_fan ─► fan_target + commands_for ─► queue_set
                                     ▼
                         handed to the MCU on its next get_down
```

## Matter Endpoints

Created in [main/matter_setup.cpp](main/matter_setup.cpp) in this order —
**append new endpoints, never insert**, so paired controllers keep their IDs:
**1** Air Purifier (Identify, Fan Control, HEPA Filter Monitoring),
**2** Air Quality Sensor (Air Quality, PM2.5 Concentration Measurement),
**3** Temperature Sensor, **4** Humidity Sensor — 3 and 4 on the 3H only.
A unit is always the same model, so its IDs never shift; a new endpoint for
one model must not renumber the other model's.
`CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=6` (root + 4 + headroom).

- **Code-driven clusters.** In esp_matter 1.6 Fan Control, HEPA Filter
  Monitoring, Air Quality, Temperature and Relative Humidity Measurement are
  served by cluster objects in the data-model provider registry.
  `attribute::update()` on them is silently ignored — set values on the
  objects (`FanControl::FindClusterOnEndpoint`, `ResourceMonitoring::GetClusterInstance`,
  `find_cluster<>()`). **PM2.5 Concentration Measurement is still
  attribute-store backed**, so `attribute::update()` is right there.
- **Measured-value ranges** must be set on every measurement endpoint (config
  default 0..0 rejects everything).
- **Features are added before `esp_matter::start()`.** The code-driven objects
  read the feature map once, when the server starts.
- **Delegates are attached twice**: in the endpoint config and again in
  `kServerReady` (`attach_delegates()`). esp_matter can only attach a delegate
  once the cluster object exists; without one, a HEPA `ResetCondition` would
  dereference null.
- **Fan Control echo suppression.** The cluster calls `OnFanDriveStateChanged`
  for every change, including the ones `refresh_fan()` makes from MCU reports.
  `s_applying_purifier_state` makes the delegate ignore those — keep every
  Fan Control setter call inside that flag.
- **FanMode vs speed writes.** The cluster itself maps FanMode → Percent/Speed
  (Low 33 %/1, Medium 66 %/8, High 100 %/15 with SpeedMax 15). `fan_target()`
  uses exactly those pairs to recognise a FanMode write (→ a named level
  1–3: manual level on the 3H, a preset rpm on the 3C); anything else is a
  speed/percent write (→ favorite level speed−1 on the 3H, `rpm_for_speed()`
  on the 3C). Speed 15 equals High. If the cluster's mapping changes in
  a future esp_matter, `purifier_model.h` constants and tests must follow.
- **Device fault** goes to General Diagnostics `ActiveHardwareFaults` on the
  root endpoint. That attribute is read from the platform's diagnostic data
  provider, so `matter_setup()` installs `PurifierDiagnosticDataProvider`
  (the ESP32 provider plus our faults) with `SetDiagnosticDataProvider()`
  **before** `node::create()`; changes go through
  `GlobalNotifyHardwareFaultsDetect()` so the attribute report and the
  `HardwareFaultChange` event fire. Mapping in `hardware_faults()`.
- **`PercentCurrent` follows the setting.** Home Assistant's fan slider shows
  `PercentCurrent`, not `PercentSetting`. `purifier::percent_current()`
  reports the set speed for named levels and fine speeds, and the motor rpm
  only in Auto and Sleep; `refresh_fan()` prefers the cluster's own
  `PercentSetting` when the purifier runs at the cluster's `SpeedSetting`.
  Deriving it from the rpm made the slider snap back (fixed after the first
  3H test).
- **3C air quality** is derived from PM2.5 (`air_quality_from_pm25()`), so a
  PM2.5 change on the 3C also raises `CHANGED_AIR_QUALITY`.
- **Null attributes during commissioning.** `kServerReady` pushes everything
  the MCU has reported so far, so a controller's first read isn't null.

## MIoT Protocol Quirks — Do Not Break

- **The MCU is the master.** The module never speaks first; it hands over one
  queued command per `get_down`. Every received line gets exactly one reply.
- **`result` parsing depends on what was handed over last** (get: value after a
  0 code; set: no value on the wire; action: `siid aiid code ...`). `Session`
  tracks this in `m_pending`; don't reorder replies. An accepted set (code 0)
  is reported through `on_properties` with the value that was sent
  (`m_pending_set_*`), so the tracked state follows at once.
- **Network state jumps the queue.** `announce_net_state()` puts
  `MIIO_net_change <state>` at the front whenever the state changes; the MCU
  drives the Wi-Fi LED from it and from `net`.
- **Unknown lowercase commands get `ok`, anything else `error`** — picky MCUs
  stall otherwise.
- **`restore`** must be answered before wiping pairings: the factory reset runs
  `RESTORE_DELAY_MS` later from an esp_timer.

## Platform Quirks — Do Not Break

- **UART**: UART0 (GPIO1/3) is the flash/console header; the MCU link is
  **UART2 on GPIO17 (TX) / GPIO16 (RX)** — `board_pins.h`.
- **`CONFIG_FREERTOS_UNICORE=y`**: the module is single-core (confirmed on a
  3H, see [dump/README.md](dump/README.md)).
- **`CONFIG_ESP_MAC_IGNORE_MAC_CRC_ERROR=y`**: the MAC eFuse checksum is
  invalid (confirmed on a 3H); without this the MAC read fails at boot.
- **WPA3 on, compatibility elements ignored** (`CONFIG_ESP_WIFI_ENABLE_WPA3_SAE=y`
  plus `wifi_config_t.sta.disable_wpa3_compatible_mode = 1`, as in
  gravity-cube-espidf). The FRITZ!Box 4040 advertises a WPA3 compatibility
  (RSN override) element in its beacon but leaves it out of handshake
  message 3; with SAE on and that element honoured, a 3H failed every join
  with reason 17 `IE_IN_4WAY_DIFFERS`. The CHIP ESP32 driver `memset`s the
  station config when it stores credentials, so `apply_station_options()` in
  `wifi_events.cpp` sets the flag (and `sae_pwe_h2e = WPA3_SAE_PWE_BOTH`) on
  the stored config at start-up, at `STA_START` and after each failed join;
  right after commissioning the first join can therefore still fail once.
  Fallback if a router misbehaves anyway: `CONFIG_ESP_WIFI_ENABLE_WPA3_SAE=n`
  (WPA2 only, confirmed working on that FRITZ!Box). The compatibility-flag
  path itself is not yet confirmed on the purifier.
- **`sdkconfig.defaults` changes need a fresh `sdkconfig`**: an existing
  `sdkconfig` wins over the defaults file. Delete it (and `build/`) after
  changing defaults.
- **No power management / light sleep** — mains powered; don't add PM locks.
- **Partition table**: [partitions_4mb.csv](partitions_4mb.csv), two app slots
  (`ota_0`, `ota_1`, 0x1F0000 each) for updates over Wi-Fi. `nvs` (Wi-Fi,
  Matter fabrics, boot_diag, remembered model) keeps the offset and size of
  the earlier single-app layout (0x9000, 0x15000) so a serial flash to the new
  table keeps the pairing — never move it. `otadata` took the unused
  `phy_init` space.
- **Updates over Wi-Fi** (`ota_server.cpp`, rules in host-tested
  `ota_policy.cpp`), modelled on gravity-cube-espidf: `POST /ota` with the app
  image (optional `Authorization: Bearer` token, `CONFIG_PURIFIER_OTA_TOKEN`
  in `main/Kconfig.projbuild`), `GET /` for version/slot/model. The httpd is
  limited to 2 sockets (`CONFIG_LWIP_MAX_SOCKETS=16` leaves room next to
  Matter). `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`: a new image is kept
  once Matter is ready, Wi-Fi is up and the MCU talks
  (`ota_policy::verdict`), checked from the `app_main` loop; otherwise it
  rolls back after 2 minutes. Not Matter OTA: Home Assistant only serves
  OTA for devices in the production DCL, not our test vendor ID.
- **Hostname** `mi-air-purifier` (`CONFIG_LWIP_LOCAL_HOSTNAME`) via DHCP.
  No mDNS name: Matter uses CHIP's minimal mDNS (`CONFIG_USE_MINIMAL_MDNS=y`),
  and Espressif's mDNS component would compete for port 5353.
- **Pairing code is per-build**: discriminator, passcode and verifier in
  `chip_project_config.h`. Change passcode and verifier together
  (`python3 tools/spake2p_verifier.py <passcode>`).
- **Never commit stock flash dumps** (`stock_*` is git-ignored): they contain
  device keys and Wi-Fi credentials. `dump/README.md` records what the 3H's
  dump showed (stock partition table, chip facts); only that file is
  committed.
- **esptool and bootloader mode**: each esptool command resets the chip out
  of the ROM bootloader unless given `--after no-reset`; the README's commands
  pass it.

## Hardware Status

**Confirmed on a 3H** (`dump/README.md` and bring-up logs, board powered
from the USB adapter): single core, 4 MB flash, invalid MAC eFuse CRC, no
flash encryption or secure boot. The link on UART2 GPIO17/GPIO16 at 115200
works; the MCU polls `get_down` every ~180 ms, answers
`MIIO_mcu_version_req` (`0017`), sends `properties_changed` on its own, and
did **not** announce `model` (detection ran on the probe). Right after boot
it answers several properties (2.2, 2.5, 3.6, 3.7, 3.8, 2.1, 2.4) with
`-4001` until it has settled; the next poll returns them. Joins a FRITZ!Box
4040 with WPA2, commissions with Home Assistant (Android), and power, presets
and the speed slider work.

**Still open:** the heartbeat (13.9 on the 3H, 9.4 on the 3C, = 60 every
60 s); the filter-reset action arguments (`4.1` with `3 0`); what favorite
level 0 means (3H); the 10 % filter-warning threshold; the polarity of 15.5
(filter door: logged "open" on a unit under test); the WPA3 compatibility
flag; an update over Wi-Fi, including a rollback; behaviour on mains with
the fan running. **Suspected:** soon after a restart a fan command went to
the MCU without any controller write (`Fan Control write: mode=2 percent=66
speed=10`, rejected with codes 1 / -4004), probably the Fan Control
cluster restoring its saved FanMode at start-up and notifying the delegate;
confirm with a full boot log before fixing.
3C only: that mode 2 is Favorite, its accepted rpm range, comfortable
Low/Medium/High presets. On the 3C, GPIO0 isn't on the header (short it on
the module). Update `purifier_model.cpp`, `board_pins.h`,
`sdkconfig.defaults` and the tests together when a capture says otherwise.
