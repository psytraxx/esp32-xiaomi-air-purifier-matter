#pragma once

// Logs Wi-Fi station connects and disconnects with the reason, a hint and the
// security mode in use (see wifi_diag.h). Call once after esp_matter::start(),
// which creates the default event loop.
// Creates the Wi-Fi station interface with `hostname` (copied) set, before
// esp_matter::start(). CHIP reuses an existing interface; created later, the
// first DHCP request after boot would go out with CONFIG_LWIP_LOCAL_HOSTNAME,
// shared by every unit.
void wifi_netif_prepare(const char *hostname);

// Registers the Wi-Fi event logging after esp_matter::start(). Re-applies
// the host name (from wifi_netif_prepare(), or `hostname` if not null)
// before each join.
void wifi_events_start(const char *hostname);
