#pragma once

// Logs Wi-Fi station connects and disconnects with the reason, a hint and the
// security mode in use (see wifi_diag.h). Call once after esp_matter::start(),
// which creates the default event loop.
void wifi_events_start();
