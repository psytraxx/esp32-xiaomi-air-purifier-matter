# Stock flash dump — Mi Air Purifier 3H

Backup of the stock firmware on the purifier's ESP32 module, taken
2026-11-10 with esptool v5.6.0. This is the only way back to Xiaomi's
firmware and Mi Home. See [Backup, flashing and restore](../README.md#backup-flashing-and-restore)
in the main README for how to restore it.

> ⚠️ **The `stock_*` files never go into git.** `.gitignore` excludes them.
> The dump holds this unit's Xiaomi device identity and keys and the Wi-Fi
> credentials it was set up with. Keep copies somewhere safe outside the
> repository (an encrypted drive or a password manager attachment). This
> README is the only file here that's committed.

## Files

| File | What it is |
|------|------------|
| `stock_full_1.bin` | Full 4 MB flash image, `read-flash 0 ALL` |
| `stock_full_2.bin` | Second read for comparison: identical to the first, so it's not committed (git-ignored, local only) |
| `stock_partitions.bin` | Partition table cut out of the dump (0x8000, 0xC00 bytes) |
| `stock_efuse_summary.txt` | `espefuse summary`: eFuses are never written, so this record is all that's kept |
| `stock_flash_id.txt` | `esptool flash-id` output |

`stock_full_1.bin` SHA-256:
`679a035cc321eb17b66b1f04e4ef3d3609f1afba956225b48e965ade605f3c94`

## Status

- [x] First full read: complete, 4194304 bytes
- [x] Sanity checks: bootloader at 0x1000 and both app images have valid
      headers, checksums and SHA-256 hashes; the partition table parses
- [x] Second read with a matching hash: both reads have the SHA-256 above
- [ ] `verify-flash 0 stock_full_1.bin` against the chip: skipped. esptool
      couldn't reconnect after the second read without a power cycle, and two
      identical full reads already prove the connection read correctly

The dump is good to restore from.

## Hardware findings (Phase 0)

| Question | Result |
|----------|--------|
| Chip | ESP32, silicon revision v1.0, 40 MHz crystal |
| Cores | **Single core**, so `CONFIG_FREERTOS_UNICORE=y` stays |
| Flash | **4 MB** (JEDEC manufacturer 0x68, device 0x4016), 3.3 V flash voltage |
| MAC eFuse CRC | **Invalid** (stored 0x75, calculated 0x10), so `CONFIG_ESP_MAC_IGNORE_MAC_CRC_ERROR=y` is needed. The MAC itself reads fine; it's in `stock_efuse_summary.txt` |
| Custom MAC (BLOCK3) | Not set (CRC invalid, value is noise) |
| Coding scheme | 3/4 |
| Flash encryption / secure boot | Off: the dump is plain and readable, and our own image can be written without keys |

## Stock partition table

```
# Name,     Type, SubType,  Offset,   Size
nvs,        data, nvs,      0x9000,   16K
otadata,    data, ota,      0xd000,   8K
phy_init,   data, phy,      0xf000,   4K
miio_fw1,   app,  ota_0,    0x10000,  1408K
miio_fw2,   app,  ota_1,    0x170000, 1408K
test,       app,  test,     0x2d0000, 76K
mimcu,      data, 253,      0x2e3000, 1M
coredump,   data, coredump, 0x3e3000, 64K
minvs,      data, 254,      0x3f8000, 16K
```

- `nvs` holds the Wi-Fi credentials, and `minvs` probably holds the Xiaomi
  device identity (DID, keys). These two make the dump unit-specific.
- `miio_fw1` and `miio_fw2` contain the **same** image (identical validation
  hash). It's Xiaomi's miio gateway, built on ESP-IDF `v3.1.4-56-g8ffb04960`.
  `otadata` is blank, so the module boots slot 1.
- `mimcu` contains **STM32 firmware**, with build paths for
  `zhimi.airpurifier.ma4.mb3` (`Device_app/Air_mb3`, `stm32f4xx_hal_msp.c`).
  This is presumably the image the gateway pushes to the main controller for
  MCU updates. It's a useful reference for the MCU's side of the MIoT
  protocol, which we never write to.
- The model string in the dump is `zhimi.airpurifier.mb3`, as expected for
  the 3H.

## Wiring notes from this session

- First attempts failed with `No serial data received` and the USB adapter
  dropping off the bus whenever the board's 3.3 V wire was reconnected. The
  board's 3.3 V rail can pull a weak adapter regulator down far enough to
  reset it. The connection came up after rewiring. A separate 3.3 V supply
  with at least 1 A, sharing GND with the adapter, avoids the problem.
- After every esptool command the chip leaves bootloader mode (esptool's
  default hard reset). Power-cycle with `GPIO0`–`GND` still shorted before the
  next command, or pass `--after no-reset`.
