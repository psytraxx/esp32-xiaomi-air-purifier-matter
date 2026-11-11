#include "miot_protocol.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace miot
{

Value Value::of_bool(bool b)
{
    Value v;
    v.m_type = Type::Bool;
    v.m_bool = b;
    return v;
}

Value Value::of_int(int64_t i)
{
    Value v;
    v.m_type = Type::Int;
    v.m_int = i;
    return v;
}

Value Value::of_float(double f)
{
    Value v;
    v.m_type = Type::Float;
    v.m_float = f;
    return v;
}

Value Value::of_string(std::string s)
{
    Value v;
    v.m_type = Type::String;
    v.m_str = std::move(s);
    return v;
}

std::optional<int64_t> parse_int(std::string_view s)
{
    if (s.empty() || s.size() > 20)
        return std::nullopt;
    size_t i = 0;
    if (s[0] == '-' || s[0] == '+')
        i = 1;
    if (i == s.size())
        return std::nullopt;
    for (size_t k = i; k < s.size(); k++)
    {
        if (s[k] < '0' || s[k] > '9')
            return std::nullopt;
    }
    std::string buf(s);
    errno = 0;
    char *end = nullptr;
    long long v = std::strtoll(buf.c_str(), &end, 10);
    if (errno != 0 || end != buf.c_str() + buf.size())
        return std::nullopt;
    return static_cast<int64_t>(v);
}

std::optional<uint16_t> parse_id(std::string_view s)
{
    if (s.empty() || s[0] == '-' || s[0] == '+')
        return std::nullopt;
    auto v = parse_int(s);
    if (!v || *v < 0 || *v > 0xFFFF)
        return std::nullopt;
    return static_cast<uint16_t>(*v);
}

static std::optional<double> parse_float(std::string_view s)
{
    if (s.empty() || s.size() > 40)
        return std::nullopt;
    // Only plain decimal notation, optionally with an exponent — no "nan",
    // "inf" or hex floats, which strtod would otherwise accept.
    bool digit = false;
    for (char c : s)
    {
        if (c >= '0' && c <= '9')
            digit = true;
        else if (c != '-' && c != '+' && c != '.' && c != 'e' && c != 'E')
            return std::nullopt;
    }
    if (!digit)
        return std::nullopt;
    std::string buf(s);
    errno = 0;
    char *end = nullptr;
    double v = std::strtod(buf.c_str(), &end);
    if (errno != 0 || end != buf.c_str() + buf.size() || !std::isfinite(v))
        return std::nullopt;
    return v;
}

Value Value::parse(std::string_view token, bool quoted)
{
    if (quoted)
        return of_string(std::string(token));
    if (token == "true")
        return of_bool(true);
    if (token == "false")
        return of_bool(false);
    if (auto i = parse_int(token))
        return of_int(*i);
    if (auto f = parse_float(token))
        return of_float(*f);
    return of_string(std::string(token));
}

std::optional<bool> Value::as_bool() const
{
    switch (m_type)
    {
    case Type::Bool:
        return m_bool;
    case Type::Int:
        return m_int != 0;
    case Type::Float:
        return m_float != 0.0;
    case Type::String:
        break;
    }
    return std::nullopt;
}

std::optional<int64_t> Value::as_int() const
{
    switch (m_type)
    {
    case Type::Bool:
        return m_bool ? 1 : 0;
    case Type::Int:
        return m_int;
    case Type::Float:
        if (m_float >= -9.2e18 && m_float <= 9.2e18)
            return static_cast<int64_t>(std::llround(m_float));
        break;
    case Type::String:
        break;
    }
    return std::nullopt;
}

std::optional<double> Value::as_float() const
{
    switch (m_type)
    {
    case Type::Bool:
        return m_bool ? 1.0 : 0.0;
    case Type::Int:
        return static_cast<double>(m_int);
    case Type::Float:
        return m_float;
    case Type::String:
        break;
    }
    return std::nullopt;
}

std::string Value::to_wire() const
{
    switch (m_type)
    {
    case Type::Bool:
        return m_bool ? "true" : "false";
    case Type::Int:
        return std::to_string(m_int);
    case Type::Float:
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", m_float);
        return buf;
    }
    case Type::String:
    {
        std::string out;
        out.reserve(m_str.size() + 2);
        out += '"';
        for (char c : m_str)
        {
            if (c == '"' || c == '\\')
                out += '\\';
            out += c;
        }
        out += '"';
        return out;
    }
    }
    return "";
}

bool Value::operator==(const Value &o) const
{
    if (m_type != o.m_type)
        return false;
    switch (m_type)
    {
    case Type::Bool:
        return m_bool == o.m_bool;
    case Type::Int:
        return m_int == o.m_int;
    case Type::Float:
        return m_float == o.m_float;
    case Type::String:
        return m_str == o.m_str;
    }
    return false;
}

std::vector<Token> tokenize(std::string_view line)
{
    std::vector<Token> out;
    size_t i = 0;
    const size_t n = line.size();
    while (i < n)
    {
        while (i < n && line[i] == ' ')
            i++;
        if (i >= n)
            break;

        Token tok;
        if (line[i] == '"')
        {
            tok.quoted = true;
            i++;
            while (i < n && line[i] != '"')
            {
                if (line[i] == '\\' && i + 1 < n)
                    i++;
                tok.text += line[i];
                i++;
            }
            if (i < n)
                i++; // closing quote
        }
        else
        {
            while (i < n && line[i] != ' ')
            {
                tok.text += line[i];
                i++;
            }
        }
        out.push_back(std::move(tok));
    }
    return out;
}

} // namespace miot
