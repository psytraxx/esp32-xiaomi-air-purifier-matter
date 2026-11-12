#pragma once

// Logs Wi-Fi station connects and disconnects with the reason, a hint and the
// security mode in use (see wifi_diag.h). Call once after esp_matter::start(),
// which creates the default event loop.
// Registers the Wi-Fi event logging and sets the station's DHCP host name
// (`hostname`, copied) before each join.
void wifi_events_start(const char *hostname);
