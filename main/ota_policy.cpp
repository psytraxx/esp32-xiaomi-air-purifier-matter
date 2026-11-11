#include "ota_policy.h"

#include <cstring>

namespace ota_policy
{

Verdict verdict(uint32_t elapsed_ms, bool matter_ready, bool wifi_up, bool mcu_alive)
{
    if (matter_ready && wifi_up && mcu_alive)
        return Verdict::Keep;
    return elapsed_ms >= VERIFY_TIMEOUT_MS ? Verdict::RollBack : Verdict::Wait;
}

bool token_matches(const char *header, const char *token)
{
    if (!token || token[0] == '\0')
        return true;
    if (!header)
        return false;
    static const char prefix[] = "Bearer ";
    const size_t prefix_len = sizeof(prefix) - 1;
    const size_t token_len = std::strlen(token);
    const size_t header_len = std::strlen(header);
    if (header_len != prefix_len + token_len || std::strncmp(header, prefix, prefix_len) != 0)
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < token_len; i++)
        diff |= static_cast<unsigned char>(header[prefix_len + i] ^ token[i]);
    return diff == 0;
}

} // namespace ota_policy
