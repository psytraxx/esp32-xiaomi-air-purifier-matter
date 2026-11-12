#include "device_identity.h"

namespace identity
{

static std::string truncate_utf8(std::string s, size_t max_len)
{
    if (s.size() <= max_len)
        return s;
    size_t cut = max_len;
    // Back up over UTF-8 continuation bytes (10xxxxxx) so a character isn't split.
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        cut--;
    s.resize(cut);
    return s;
}

std::string node_label(std::string_view configured, std::string_view base, purifier::Model model)
{
    // Ignore a name of only spaces.
    size_t first = configured.find_first_not_of(' ');
    if (first != std::string_view::npos)
    {
        size_t last = configured.find_last_not_of(' ');
        return truncate_utf8(std::string(configured.substr(first, last - first + 1)), NODE_LABEL_MAX);
    }
    std::string label(base);
    label += ' ';
    label += purifier::model_name(model);
    return truncate_utf8(label, NODE_LABEL_MAX);
}

bool should_write_label(std::string_view current, std::string_view desired, std::string_view last_written,
                        std::string_view automatic)
{
    if (current == desired)
        return false;
    return current.empty() || (!last_written.empty() && current == last_written) || current == automatic;
}

// Letters and digits, lower case; every other run of bytes becomes one '-'.
static std::string to_host_label(std::string_view text)
{
    std::string out;
    bool dash = false;
    for (char ch : text)
    {
        char c = ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            if (dash && !out.empty())
                out += '-';
            out += c;
            dash = false;
        }
        else
        {
            dash = true;
        }
    }
    return out;
}

std::string hostname(std::string_view configured, std::string_view base, purifier::Model model, const uint8_t mac[6])
{
    std::string out = to_host_label(configured);
    if (out.empty())
    {
        static const char k_hex[] = "0123456789abcdef";
        out = to_host_label(base);
        if (model != purifier::Model::Unknown)
            out += '-' + to_host_label(purifier::model_name(model));
        out += '-';
        for (int i = 4; i < 6; ++i)
        {
            out += k_hex[mac[i] >> 4];
            out += k_hex[mac[i] & 0x0F];
        }
    }
    if (out.size() > HOSTNAME_MAX)
        out.resize(HOSTNAME_MAX);
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    return out;
}

} // namespace identity
