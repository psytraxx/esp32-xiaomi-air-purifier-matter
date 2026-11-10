#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// The serial link to the purifier's STM32: owns the UART, the MIoT protocol
// session and the tracked device state.
//
// A dedicated task reads lines from the MCU and answers each one; it also
// queues the periodic property poll and the heartbeat. Everything here is
// safe to call from any task (one internal mutex guards the session and the
// state). Callbacks run on the link task and must not block — the Matter side
// only records what changed and schedules work onto the CHIP task.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>

#include "purifier_model.h"

struct PurifierLinkCallbacks
{
    // Some tracked state changed; `changed` is a mask of purifier::Changed.
    void (*on_state_changed)(uint32_t changed) = nullptr;
    // The user asked for a network reset on the purifier itself.
    void (*on_restore)() = nullptr;
    // The MCU's model became known: announced by the MCU, or told apart by
    // which probe property it answers. Reported once per model seen.
    void (*on_model_detected)(purifier::Model model) = nullptr;
};

// Install the UART driver and start the link task. Call once, before Matter
// starts. The link answers the MCU and probes its model straight away; it
// polls properties only once purifier_link_set_model() has chosen a profile.
void purifier_link_start(const PurifierLinkCallbacks &callbacks);

// Choose the property table to use (normally the detected or cached model)
// and start polling. Call once.
void purifier_link_set_model(purifier::Model model);

// The profile in use, or nullptr before purifier_link_set_model().
const purifier::Profile *purifier_link_profile();

// Connectivity, reported to the MCU on its `net` requests (Wi-Fi LED).
void purifier_link_set_network(bool commissioned, bool wifi_connected);

// A copy of the current tracked state.
purifier::State purifier_link_state();

// Requests from Matter. Each queues the needed set_properties/action for the
// MCU's next poll.
void purifier_link_apply_fan(const purifier::FanDrive &drive);
void purifier_link_set_sleep(bool sleep_wind);
void purifier_link_reset_filter();

// True while the MCU has talked to us within the last few seconds.
bool purifier_link_mcu_alive();
