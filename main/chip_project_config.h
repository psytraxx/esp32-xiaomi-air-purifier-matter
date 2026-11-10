#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Per-build Matter commissioning identity (decision D4, as in the plant
// monitor).
//
// Without this file every flashed device uses the CHIP SDK's shared test setup
// values (passcode 20202021 / discriminator 0xF00). Loaded into the CHIP build
// via CONFIG_CHIP_PROJECT_CONFIG in sdkconfig.defaults.
//
// To give a device its own pairing code, change BOTH values below before
// flashing it:
//   1. Pick a new discriminator (0x000–0xFFF; must differ between devices that
//      are commissioned at the same time).
//   2. Pick a new passcode (1–99999998, not a trivial value like 12345678) and
//      regenerate the matching verifier:
//          python3 tools/spake2p_verifier.py <passcode>
//      The passcode and verifier MUST be kept in sync — a mismatch makes
//      commissioning fail.
// ─────────────────────────────────────────────────────────────────────────────

#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR 0x8CA

#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE 79983961

// Generated with: python3 tools/spake2p_verifier.py 79983961
#define CHIP_DEVICE_CONFIG_USE_TEST_SPAKE2P_VERIFIER                          \
    "zGxlcAg6OAuYu9bOVkip4jsnSqqpOMeVbyosRY98mj0Ebg4Ecy2D3FWjety0q0MFMOPA/" \
    "Vm6kwNvkZrhWSxOiCq+M0EuQLMyr4uWb+cAkkQkUzRBWLN8XvZh4LOMSzgHQw=="

// ─────────────────────────────────────────────────────────────────────────────
// Basic Information cluster identity, shown by controllers.
//
// Hardware version and serial number come from Kconfig
// (CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION / CONFIG_USE_TEST_SERIAL_NUMBER in
// sdkconfig.defaults), not from #defines here: CHIPDevicePlatformConfig.h
// defines those itself after this file is included, and a second definition
// is a hard error under CI's -Werror.
//
// The firmware version string is left at the SDK default, the git describe of
// the build.
// ─────────────────────────────────────────────────────────────────────────────

#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "psytraxx"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "Mi Air Purifier 3H (Matter)"
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION_STRING "zhimi.airpurifier.mb3"
