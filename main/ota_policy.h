#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Rules for updates over Wi-Fi (ota_server.cpp), kept free of ESP-IDF so they
// are unit-tested on the host.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>

namespace ota_policy
{

// How long a freshly updated image gets to prove itself before the
// bootloader goes back to the previous one.
inline constexpr uint32_t VERIFY_TIMEOUT_MS = 120 * 1000;

enum class Verdict : uint8_t
{
    Wait,     // not everything is up yet, still within the timeout
    Keep,     // Matter is up, on Wi-Fi and talking to the purifier
    RollBack, // timed out
};

// A new image is kept once the Matter server is ready, the station is on
// Wi-Fi (so the next update can reach it) and the purifier's MCU is talking
// to it; otherwise it is rolled back after VERIFY_TIMEOUT_MS.
Verdict verdict(uint32_t elapsed_ms, bool matter_ready, bool wifi_up, bool mcu_alive);

// The update request's Authorization header against the configured token.
// An empty token accepts any request. Otherwise the header must be exactly
// "Bearer <token>"; compared in constant time.
bool token_matches(const char *authorization_header, const char *token);

} // namespace ota_policy
