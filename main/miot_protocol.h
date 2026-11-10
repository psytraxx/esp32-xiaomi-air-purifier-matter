#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Xiaomi MIoT serial protocol: values, tokenizer and line formatting.
//
// Pure C++ with no ESP-IDF or CHIP dependencies, so it is unit-tested on the
// host (test/). Lines on the wire are ASCII, space-separated, terminated by
// '\r', at most 512 bytes. String values are wrapped in double quotes and may
// contain spaces. See README.md "MCU ↔ ESP32 serial protocol".
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace miot
{

// Longest line either side may send, excluding the '\r' terminator.
inline constexpr size_t MAX_LINE_LENGTH = 512;

// One property is addressed by its service ID and property ID (siid.piid).
struct PropertyId
{
    uint16_t siid = 0;
    uint16_t piid = 0;

    constexpr bool operator==(const PropertyId &o) const { return siid == o.siid && piid == o.piid; }
    constexpr bool operator!=(const PropertyId &o) const { return !(*this == o); }
};

// An action is addressed by its service ID and action ID (siid.aiid).
struct ActionId
{
    uint16_t siid = 0;
    uint16_t aiid = 0;
};

// A property value as it appears on the wire.
class Value
{
public:
    enum class Type : uint8_t
    {
        Bool,
        Int,
        Float,
        String,
    };

    static Value of_bool(bool b);
    static Value of_int(int64_t i);
    static Value of_float(double f);
    static Value of_string(std::string s);

    // Interpret one token received from the MCU. `quoted` tokens are always
    // strings; unquoted ones are bool ("true"/"false"), integer, float, or —
    // if none of those parse — a bare string.
    static Value parse(std::string_view token, bool quoted);

    Type type() const { return m_type; }

    // Lenient accessors: numeric types convert into each other and bools read
    // as 0/1. Strings never convert. Empty when the value has no such reading.
    std::optional<bool> as_bool() const;
    std::optional<int64_t> as_int() const;
    std::optional<double> as_float() const;
    const std::string &as_string() const { return m_str; }

    // Render for an outgoing command: true/false, integer, shortest float,
    // or a double-quoted string.
    std::string to_wire() const;

    bool operator==(const Value &o) const;
    bool operator!=(const Value &o) const { return !(*this == o); }

private:
    Type m_type = Type::Int;
    bool m_bool = false;
    int64_t m_int = 0;
    double m_float = 0.0;
    std::string m_str;
};

struct Token
{
    std::string text;
    bool quoted = false;
};

// Split one line (without its '\r') into tokens. Runs of spaces separate
// tokens; a double-quoted run becomes one token with the quotes removed and
// `quoted` set. A backslash inside quotes escapes the next character. An
// unterminated quote takes the rest of the line.
std::vector<Token> tokenize(std::string_view line);

// Parse an unsigned decimal ID (siid/piid/aiid/eiid). Empty on anything else.
std::optional<uint16_t> parse_id(std::string_view s);

// Parse a signed decimal integer. Empty on anything else.
std::optional<int64_t> parse_int(std::string_view s);

} // namespace miot
