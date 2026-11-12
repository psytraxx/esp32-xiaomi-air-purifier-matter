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

} // namespace identity
