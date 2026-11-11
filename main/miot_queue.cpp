#include "miot_queue.h"

namespace miot
{

std::string Command::render() const
{
    std::string out;
    switch (kind)
    {
    case Kind::Get:
        out = "get_properties";
        for (const auto &id : gets)
            out += " " + std::to_string(id.siid) + " " + std::to_string(id.piid);
        break;
    case Kind::Set:
        out = "set_properties " + std::to_string(set_id.siid) + " " + std::to_string(set_id.piid) + " " + set_value.to_wire();
        break;
    case Kind::Action:
        out = "action " + std::to_string(action.siid) + " " + std::to_string(action.aiid);
        if (!text.empty())
            out += " " + text;
        break;
    case Kind::Raw:
        out = text;
        break;
    }
    return out;
}

void CommandQueue::make_room()
{
    if (m_capacity == 0 || m_items.size() < m_capacity)
        return;
    for (auto it = m_items.begin(); it != m_items.end(); ++it)
    {
        if (it->kind == Command::Kind::Get)
        {
            m_items.erase(it);
            m_dropped++;
            return;
        }
    }
    m_items.pop_front();
    m_dropped++;
}

void CommandQueue::push_back(Command c)
{
    make_room();
    m_items.push_back(std::move(c));
}

void CommandQueue::push_set(PropertyId id, Value value)
{
    for (auto &c : m_items)
    {
        if (c.kind == Command::Kind::Set && c.set_id == id)
        {
            c.set_value = std::move(value);
            return;
        }
    }
    Command c;
    c.kind = Command::Kind::Set;
    c.set_id = id;
    c.set_value = std::move(value);
    push_back(std::move(c));
}

void CommandQueue::push_get(std::vector<PropertyId> ids)
{
    if (ids.empty())
        return;
    for (const auto &c : m_items)
    {
        if (c.kind == Command::Kind::Get && c.gets == ids)
            return;
    }
    Command c;
    c.kind = Command::Kind::Get;
    c.gets = std::move(ids);
    push_back(std::move(c));
}

void CommandQueue::push_action(ActionId id, std::string args)
{
    Command c;
    c.kind = Command::Kind::Action;
    c.action = id;
    c.text = std::move(args);
    push_back(std::move(c));
}

void CommandQueue::push_raw(std::string text, bool front)
{
    Command c;
    c.kind = Command::Kind::Raw;
    c.text = std::move(text);
    if (front)
    {
        make_room();
        m_items.push_front(std::move(c));
    }
    else
    {
        push_back(std::move(c));
    }
}

std::optional<Command> CommandQueue::pop()
{
    if (m_items.empty())
        return std::nullopt;
    Command c = std::move(m_items.front());
    m_items.pop_front();
    return c;
}

std::vector<std::vector<PropertyId>> chunk_gets(const std::vector<PropertyId> &ids, size_t max_props)
{
    // "down get_properties" plus " <siid> <piid>" per property must fit.
    static constexpr size_t PREFIX = sizeof("down get_properties") - 1;
    std::vector<std::vector<PropertyId>> out;
    std::vector<PropertyId> cur;
    size_t len = PREFIX;
    for (const auto &id : ids)
    {
        size_t part = 2 + std::to_string(id.siid).size() + std::to_string(id.piid).size();
        if (!cur.empty() && (cur.size() >= max_props || len + part > MAX_LINE_LENGTH))
        {
            out.push_back(std::move(cur));
            cur.clear();
            len = PREFIX;
        }
        cur.push_back(id);
        len += part;
    }
    if (!cur.empty())
        out.push_back(std::move(cur));
    return out;
}

} // namespace miot
