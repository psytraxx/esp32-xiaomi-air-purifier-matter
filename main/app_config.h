#pragma once

#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────────
// Tunable application constants. Pin assignments live in board_pins.h.
// ─────────────────────────────────────────────────────────────────────────────

// ── Purifier model ──────────────────────────────────────────────────────────

// Which purifier this firmware drives: 0 = detect (default), 1 = 3H, 2 = 3C
// (values of purifier::Model). Override without editing the file:
//   idf.py -DPURIFIER_MODEL=2 build
// main/CMakeLists.txt only forwards the define when explicitly set, so this
// header stays the source of the default.
#ifndef PURIFIER_MODEL
#define PURIFIER_MODEL 0
#endif

// On a boot without a remembered model, how long to wait for the MCU to
// announce itself or answer the model probe before falling back to
// FALLBACK_MODEL (not remembered, so detection is retried next boot).
inline constexpr uint32_t MODEL_DETECT_TIMEOUT_MS = 15000;
#define FALLBACK_MODEL 1 // 3H

// ── MCU link ────────────────────────────────────────────────────────────────

// 1 = log every line to and from the MCU on the console ("<- ..." / "-> ...")
// and each property it reports. For bring-up and protocol captures; noisy, as
// the MCU polls several times a second. Enable without editing the file:
//   idf.py -DPURIFIER_LINK_DEBUG=1 build
#ifndef PURIFIER_LINK_DEBUG
#define PURIFIER_LINK_DEBUG 0
#endif

// How often all mapped properties are re-read from the MCU, as a safety net
// for changes the MCU doesn't report on its own.
inline constexpr uint32_t POLL_INTERVAL_MS = 30000;

// How often the stock gateway writes the heartbeat property (13.9 on the 3H,
// 9.4 on the 3C).
inline constexpr uint32_t HEARTBEAT_INTERVAL_MS = 60000;

// The MCU polls get_down every 100–200 ms; this much silence means it has
// stopped talking (reset, fault, or a wiring problem).
inline constexpr uint32_t MCU_SILENCE_TIMEOUT_MS = 5000;

// A partial line with no new byte for this long is dropped.
inline constexpr uint32_t LINK_LINE_TIMEOUT_MS = 200;

// UART read timeout per loop iteration of the link task.
inline constexpr uint32_t LINK_READ_TIMEOUT_MS = 20;

inline constexpr int LINK_UART_RX_BUF = 1024;
inline constexpr int LINK_UART_TX_BUF = 1024;
inline constexpr uint32_t LINK_TASK_STACK = 6144;
inline constexpr uint32_t LINK_TASK_PRIORITY = 6;

// ── Matter ──────────────────────────────────────────────────────────────────

// Commissioning window length (s) when it is reopened after the last fabric
// was removed.
inline constexpr uint16_t COMMISSIONING_WINDOW_TIMEOUT_S = 300;

// Delay between answering the MCU's `restore` and wiping the Matter pairing,
// so the reply leaves the UART before the restart.
inline constexpr uint32_t RESTORE_DELAY_MS = 500;

// How often the main loop logs free heap (for the RAM-headroom soak test).
inline constexpr uint32_t HEAP_LOG_INTERVAL_MS = 5 * 60 * 1000;

// Initial NodeLabel shown in controllers, followed by the model ("Air
// Purifier 3H"); a name set there wins.
#define NODE_LABEL "Air Purifier"

// Time zone for the MCU's `time` requests (POSIX TZ string).
#define APP_TIMEZONE "UTC0"

// SNTP server used to set the clock (LastChangedTime, MCU `time`).
#define APP_SNTP_SERVER "pool.ntp.org"
