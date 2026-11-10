#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// GPIO assignments of the 3H's ESP-WROOM-32D module. Single source of truth.
// From community reverse engineering; to verify on hardware (Phase 0).
// ─────────────────────────────────────────────────────────────────────────────

#include "driver/uart.h"

// Serial link to the STM32F412 main controller.
#define PURIFIER_UART_NUM UART_NUM_2
#define PIN_MCU_TX 17 // ESP32 TX → MCU RX
#define PIN_MCU_RX 16 // ESP32 RX ← MCU TX
#define PURIFIER_UART_BAUD 115200

// UART0 (GPIO1 TX / GPIO3 RX) is the flash/console header; GPIO0 is the
// strapping pin shorted to GND there to enter the ROM bootloader.
