#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Per-unit identity set in menuconfig ("Air purifier" menu): the Matter pairing
// discriminator and passcode, and the device name shown in controllers.
//
// The checks are constexpr so a bad menuconfig value fails the build (see the
// static_asserts in commissionable_data.cpp). Pure C++ — unit-tested on the
// host.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "purifier_model.h"

namespace identity
{

// Matter spec 5.1.7.1: an 8-digit passcode 1–99999998, excluding the trivial
// values below.
constexpr bool is_valid_passcode(uint32_t passcode)
{
    if (passcode < 1 || passcode > 99999998)
        return false;
    constexpr uint32_t k_invalid[] = { 11111111, 22222222, 33333333, 44444444, 55555555,
                                       66666666, 77777777, 88888888, 12345678, 87654321 };
    for (uint32_t bad : k_invalid)
    {
        if (passcode == bad)
            return false;
    }
    return true;
}

// 12-bit discriminator.
constexpr bool is_valid_discriminator(uint32_t discriminator)
{
    return discriminator <= 0xFFF;
}

// Longest NodeLabel the Basic Information cluster accepts.
inline constexpr size_t NODE_LABEL_MAX = 32;

// The name shown in controllers: the configured name if set, otherwise
// "<base> <model>" (e.g. "Air Purifier 3H"). Cut to NODE_LABEL_MAX bytes
// without splitting a UTF-8 character.
std::string node_label(std::string_view configured, std::string_view base, purifier::Model model);

// Whether to write `desired` into NodeLabel at start-up. The current label is
// the firmware's own if it is empty, the one it wrote last time, or the
// automatic "<base> <model>" label (written by firmware that didn't record
// its label yet); then it follows the menuconfig name. Anything else was set
// from a controller and is kept.
bool should_write_label(std::string_view current, std::string_view desired, std::string_view last_written,
                        std::string_view automatic);

} // namespace identity
