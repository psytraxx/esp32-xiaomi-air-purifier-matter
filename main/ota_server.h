#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Firmware updates over Wi-Fi:
//
//   curl -H "Authorization: Bearer <token>" --data-binary @build/air_purifier_matter.bin http://<purifier>/ota
//
// The image goes into the other app slot and boots. A freshly updated image
// has to prove itself (ota_policy::verdict: Matter up, on Wi-Fi, MCU talking)
// within two minutes, or the bootloader goes back to the previous one, which
// can take updates again.
//
// GET / answers with the running version, app slot and purifier model.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>

// Start the HTTP update server on port 80. Call once after esp_matter::start().
void ota_server_start();

// True while the running image is a fresh update that hasn't been kept yet.
bool ota_pending_verify();

// Decide on a pending image: keep it, roll back (restarts) or wait. Call
// regularly from the main loop; does nothing once the image is kept.
void ota_check_image(uint32_t elapsed_ms, bool matter_ready, bool wifi_up, bool mcu_alive);

// Keep a fresh update before a restart the firmware makes on purpose (a model
// change, a factory reset). Without this the bootloader would treat the
// restart as a failed first boot and go back to the previous image. Safe to
// call at any time, also before ota_server_start(); `why` is logged.
void ota_keep_before_restart(const char *why);
