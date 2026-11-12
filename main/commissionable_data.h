#pragma once

// Serves the pairing discriminator and passcode set in menuconfig ("Air
// purifier" menu) to the Matter stack, deriving the SPAKE2+ verifier from the
// passcode, so a unit's pairing code is changed in menuconfig alone.
// Requires CONFIG_CUSTOM_COMMISSIONABLE_DATA_PROVIDER. Call before
// esp_matter::start().
void commissionable_data_install();
