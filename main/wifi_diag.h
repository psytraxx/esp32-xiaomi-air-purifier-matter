#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Wi-Fi connection diagnostics: names for ESP-IDF disconnect reasons and
// security modes, a plain-language hint for the common failures, and a filter
// that keeps a reconnect loop from flooding the console.
//
// The Matter SDK's own logging is set to errors only, which hides why the
// station keeps dropping; wifi_events.cpp uses this to log it instead.
// Numeric values follow ESP-IDF v6.1 (esp_wifi_types_generic.h:
// wifi_err_reason_t, wifi_auth_mode_t). Pure C++ — unit-tested on the host.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>

namespace wifi_diag
{

// "IE_IN_4WAY_DIFFERS" for 17, "UNKNOWN" for codes not in the table.
const char *reason_name(uint8_t reason);

// What usually causes this reason and what to change; nullptr when there's
// no useful advice.
const char *reason_hint(uint8_t reason);

// "WPA2_PSK" for 3, "UNKNOWN" outside the table.
const char *authmode_name(int authmode);

// Decides which disconnects to log while the station retries: the first one
// of a run of identical reasons, then every `every`-th repeat, and a summary
// of the previous run whenever the reason changes or the station connects.
class DisconnectFilter
{
public:
    explicit DisconnectFilter(uint32_t every = 50) : m_every(every) {}

    struct Decision
    {
        bool log = false;                // log this disconnect
        uint32_t repeats = 0;            // how many times this reason has occurred in a row
        uint8_t previous_reason = 0;     // reason of the run that just ended
        uint32_t previous_count = 0;     // its length; 0 if no run ended
    };

    Decision on_disconnect(uint8_t reason);

    // A successful connection ends the current run. Returns its length
    // (0 if there was none) and the reason through `reason`.
    uint32_t on_connected(uint8_t &reason);

private:
    uint32_t m_every;
    uint8_t m_reason = 0;
    uint32_t m_count = 0;
};

} // namespace wifi_diag
