# Changelog

Entries are grouped by date, newest first.

## 2026-11-10

### Added
- An implementation plan (docs/PLAN.md) laying out the steps from a stock purifier to a working Matter one, the decisions made so far, and a table showing which Matter feature each purifier function will appear as.
- Step-by-step guide to backing up the purifier's original Wi-Fi firmware before flashing, checking that the backup is good, and putting it back if anything goes wrong, with a troubleshooting table.
- Project README describing how the purifier works inside, how its two chips talk to each other, how its settings and sensors will appear in Matter, and how to back up and flash the Wi-Fi module.
- The README now records the build setup, which matches the plant monitor project (ESP-IDF 6.1 and Espressif's Matter SDK), and how this purifier differs from it: Wi-Fi instead of Thread, always on instead of sleeping, and a few quirks of Xiaomi's Wi-Fi module.

### Changed
- Power, modes, fan speed and sleep mode will all be controlled through Matter's standard fan controls, rather than a separate on/off switch. Child lock, beeper and display brightness won't be exposed and stay controllable on the purifier itself.
- The README's diagrams are now drawn with Mermaid, so GitHub renders them as real diagrams.
- The README now makes clear that this firmware is written from scratch on ESP-IDF and doesn't use ESPHome. The ESPHome projects are credited only as prior research into the hardware, and setting IDs now point to Xiaomi's own published spec.
