#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Boot diagnostics persisted across power cycles / panics in the default NVS
// partition. The module sits inside a closed purifier, so a field failure is
// otherwise hard to diagnose — and an esp_matter::start() failure on a corrupt
// NVS would reboot-loop forever with no record of why.
//
// Call boot_diag_init() once, very early in app_main(). It logs the reset
// reason and the count of consecutive un-acknowledged boots.

// Read + log the persisted state; bump the consecutive-boot counter.
void boot_diag_init(void);

// Consecutive boots that have NOT yet called boot_diag_mark_healthy().
// A large value means the device is stuck rebooting before it reaches a
// steady state.
uint32_t boot_diag_boot_streak(void);

// Call once the device has reached a known-good state (Matter server ready).
// Resets the consecutive-boot counter so the next unclean boot starts from 0.
void boot_diag_mark_healthy(void);

// Record that esp_matter::start() (or another critical init step) failed.
// Returns true if this has now happened on enough consecutive boots that the
// caller should erase the Matter NVS partition before restarting, to break a
// corruption-induced reboot loop.
bool boot_diag_record_matter_fault(void);

#ifdef __cplusplus
}
#endif
