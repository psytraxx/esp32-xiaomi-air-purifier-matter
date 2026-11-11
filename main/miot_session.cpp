#include "miot_session.h"

#include <cctype>

namespace miot
{

const char *net_state_str(NetState s)
{
    switch (s)
    {
    case NetState::Offline:
        return "offline";
    case NetState::Unprovisioned:
        return "unprov";
    case NetState::AccessPoint:
        return "uap";
    case NetState::Local:
        return "local";
    case NetState::Cloud:
        return "cloud";
    }
    return "offline";
}

NetState net_state_for(bool commissioned, bool wifi_connected)
{
    if (!commissioned)
        return NetState::Unprovisioned;
    return wifi_connected ? NetState::Cloud : NetState::Offline;
}

Session::Session(Hooks hooks) : m_hooks(std::move(hooks)) {}

void Session::log(const std::string &msg) const
{
    if (m_hooks.log)
        m_hooks.log(msg);
}

void Session::start()
{
    announce_net_state();
    m_queue.push_raw("MIIO_mcu_version_req");
}

void Session::announce_net_state()
{
    if (!m_hooks.net_state)
        return;
    NetState s = m_hooks.net_state();
    if (m_net_announced && s == m_last_net)
        return;
    m_net_announced = true;
    m_last_net = s;
    m_queue.push_raw(std::string("MIIO_net_change ") + net_state_str(s), /*front=*/true);
}

void Session::queue_get(const std::vector<PropertyId> &ids)
{
    for (auto &chunk : chunk_gets(ids))
        m_queue.push_get(std::move(chunk));
}

std::string Session::handle_line(std::string_view line, uint32_t now_ms)
{
    m_last_rx_ms = now_ms;
    m_seen = true;

    // Pick up a network-state change before answering, so it is the next
    // command the MCU receives.
    announce_net_state();

    std::vector<Token> t = tokenize(line);
    if (t.empty())
        return "error";
    const std::string &cmd = t[0].text;

    if (cmd == "get_down")
    {
        auto next = m_queue.pop();
        if (!next)
            return "down none";
        m_pending = next->kind;
        m_has_pending = true;
        if (next->kind == Command::Kind::Set)
        {
            m_pending_set_id = next->set_id;
            m_pending_set_value = next->set_value;
        }
        return "down " + next->render();
    }
    if (cmd == "properties_changed")
    {
        handle_properties_changed(t);
        return "ok";
    }
    if (cmd == "result")
    {
        handle_result(t);
        return "ok";
    }
    if (cmd == "event_occured" || cmd == "event_occurred")
    {
        handle_event(t);
        return "ok";
    }
    if (cmd == "net")
        return net_state_str(m_hooks.net_state ? m_hooks.net_state() : NetState::Offline);
    if (cmd == "time")
    {
        bool posix = t.size() > 1 && t[1].text == "posix";
        return m_hooks.time_reply ? m_hooks.time_reply(posix) : "0";
    }
    if (cmd == "mac")
        return m_hooks.mac_reply ? m_hooks.mac_reply() : "error";
    if (cmd == "model")
    {
        if (t.size() > 1)
        {
            m_model = t[1].text;
            log("MCU model: " + m_model);
            if (m_hooks.on_model)
                m_hooks.on_model(m_model);
        }
        return "ok";
    }
    if (cmd == "mcu_version")
    {
        if (t.size() > 1)
        {
            m_mcu_version = t[1].text;
            log("MCU version: " + m_mcu_version);
        }
        return "ok";
    }
    if (cmd == "error")
    {
        std::string msg = "MCU error:";
        for (size_t i = 1; i < t.size(); i++)
            msg += " " + t[i].text;
        log(msg);
        return "ok";
    }
    if (cmd == "restore")
    {
        log("MCU requested a network reset (restore)");
        if (m_hooks.on_restore)
            m_hooks.on_restore();
        return "ok";
    }

    // Something we don't know. A command-shaped word (lowercase letters)
    // gets "ok" so the MCU carries on; anything else is line noise or debug
    // output and gets "error", as the spec requires for unrecognised input.
    bool command_shaped = cmd.size() >= 3 && std::islower(static_cast<unsigned char>(cmd[0])) &&
                          std::islower(static_cast<unsigned char>(cmd[1])) &&
                          std::islower(static_cast<unsigned char>(cmd[2]));
    log(std::string(command_shaped ? "Unknown MCU command: " : "Invalid MCU line: ") + std::string(line));
    return command_shaped ? "ok" : "error";
}

// properties_changed <siid> <piid> <value> [<siid> <piid> <value>]...
void Session::handle_properties_changed(const std::vector<Token> &t)
{
    std::vector<PropertyUpdate> updates;
    for (size_t i = 1; i + 2 < t.size(); i += 3)
    {
        auto siid = parse_id(t[i].text);
        auto piid = parse_id(t[i + 1].text);
        if (!siid || !piid)
        {
            log("Malformed properties_changed");
            break;
        }
        updates.push_back({ { *siid, *piid }, Value::parse(t[i + 2].text, t[i + 2].quoted) });
    }
    if (!updates.empty() && m_hooks.on_properties)
        m_hooks.on_properties(updates);
}

// Result formats, by what was handed over last:
//   get:    result <siid> <piid> <code> [<value>]...   value only when code is 0
//   set:    result <siid> <piid> <code>...
//   action: result <siid> <aiid> <code> [<piid> <value>]...
void Session::handle_result(const std::vector<Token> &t)
{
    Command::Kind kind = m_has_pending ? m_pending : Command::Kind::Set;
    m_has_pending = false;

    if (kind == Command::Kind::Action)
    {
        if (t.size() < 4)
            return;
        auto code = parse_int(t[3].text);
        if (!code || *code != 0)
            log("Action " + t[1].text + "." + t[2].text + " failed: " + t[3].text);
        return;
    }

    if (kind == Command::Kind::Raw)
        return; // e.g. MIIO_mcu_version_req: nothing to parse

    std::vector<PropertyUpdate> updates;
    size_t i = 1;
    while (i + 2 < t.size())
    {
        auto siid = parse_id(t[i].text);
        auto piid = parse_id(t[i + 1].text);
        auto code = parse_int(t[i + 2].text);
        if (!siid || !piid || !code)
        {
            log("Malformed result");
            break;
        }
        i += 3;
        PropertyId id{ *siid, *piid };
        if (*code != 0)
        {
            if (kind == Command::Kind::Set && m_hooks.on_set_failed)
                m_hooks.on_set_failed(id, static_cast<int>(*code));
            log("Property " + std::to_string(id.siid) + "." + std::to_string(id.piid) + " result code " + std::to_string(*code));
            continue;
        }
        if (kind == Command::Kind::Get)
        {
            if (i >= t.size())
                break;
            updates.push_back({ id, Value::parse(t[i].text, t[i].quoted) });
            i += 1;
        }
        else if (kind == Command::Kind::Set && id == m_pending_set_id)
        {
            updates.push_back({ id, m_pending_set_value });
        }
    }
    if (!updates.empty() && m_hooks.on_properties)
        m_hooks.on_properties(updates);
}

// event_occured <siid> <eiid> [<piid> <value>]...
void Session::handle_event(const std::vector<Token> &t)
{
    if (t.size() < 3)
        return;
    auto siid = parse_id(t[1].text);
    auto eiid = parse_id(t[2].text);
    if (!siid || !eiid)
        return;
    std::vector<PropertyUpdate> args;
    for (size_t i = 3; i + 1 < t.size(); i += 2)
    {
        auto piid = parse_id(t[i].text);
        if (!piid)
            break;
        args.push_back({ { *siid, *piid }, Value::parse(t[i + 1].text, t[i + 1].quoted) });
    }
    if (m_hooks.on_event)
        m_hooks.on_event(*siid, *eiid, args);
}

} // namespace miot
