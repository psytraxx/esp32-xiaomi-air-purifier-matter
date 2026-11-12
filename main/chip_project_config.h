#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Matter project configuration, loaded into the CHIP build via
// CONFIG_CHIP_PROJECT_CONFIG in sdkconfig.defaults.
//
// The pairing code (discriminator and passcode) is NOT set here: it comes
// from menuconfig ("Air purifier" menu) and is served by
// main/commissionable_data.cpp, which derives the SPAKE2+ verifier itself.
// ─────────────────────────────────────────────────────────────────────────────

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
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "Mi Air Purifier 3H/3C (Matter)"
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION_STRING "ESP-WROOM-32D"
