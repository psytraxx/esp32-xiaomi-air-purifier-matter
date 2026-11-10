#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Matter node, endpoints and the bridge between Matter and the purifier link.
//
// Endpoints (IDs in creation order — append new ones, never insert):
//   1 Air Purifier: Identify, Fan Control, HEPA Filter Monitoring
//   2 Air Quality Sensor: Air Quality, PM2.5 Concentration Measurement
//   3 Temperature Sensor
//   4 Humidity Sensor
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// Bits set on the event group passed to matter_setup().
#define MATTER_BIT_SERVER_READY (1u << 0)
#define MATTER_BIT_COMMISSIONED (1u << 1)

// Create the node and endpoints, wire the purifier link in both directions and
// start the Matter stack. Restarts the chip if the stack can't come up.
void matter_setup(EventGroupHandle_t events);

// The purifier link reports a state change (any task). Schedules an attribute
// refresh on the CHIP task; repeated calls before it runs are coalesced.
void matter_on_purifier_changed(uint32_t changed);

// Wipe all Matter pairings and restart into commissioning mode.
void matter_factory_reset();
