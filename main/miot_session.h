#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// The module side of the MIoT serial protocol.
//
// The MCU sends one line at a time and waits for exactly one reply line;
// handle_line() takes a received line and returns the reply. Everything the
// rest of the firmware needs to know (property values, events, a factory reset
// request) comes out through the Hooks callbacks, and everything it wants the
// MCU to do goes in through the queue_*() calls, to be handed over on the
// MCU's next `get_down` poll.
//
// Not thread-safe by itself: the ESP-IDF glue (miot_uart.cpp) serialises all
// calls with a mutex. Hooks are called from inside handle_line(), so they must
// not block; they may call the queue_*() functions.
//
// Pure C++ — unit-tested on the host.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "miot_protocol.h"
#include "miot_queue.h"

namespace miot
{

struct PropertyUpdate
{
    PropertyId id;
    Value value;
};

// Network states the MCU understands in reply to `net`. It shows them on the
// Wi-Fi LED.
enum class NetState : uint8_t
{
    Offline,     // was provisioned, network currently unreachable
    Unprovisioned,
    AccessPoint, // stock firmware's setup hotspot ("uap"); unused here
    Local,       // on the network, no cloud
    Cloud,       // fully connected
};

const char *net_state_str(NetState s);

// What the MCU should be told for a given connectivity state. "Cloud" is the
// state in which the stock firmware shows a steady LED, so a commissioned
// device with Wi-Fi up reports it even though there is no Xiaomi cloud.
NetState net_state_for(bool commissioned, bool wifi_connected);

class Session
{
public:
    struct Hooks
    {
        // Properties reported by the MCU, either unsolicited
        // (properties_changed) or as the answer to a queued get.
        std::function<void(const std::vector<PropertyUpdate> &)> on_properties;
        // A set the MCU rejected (non-zero result code).
        std::function<void(PropertyId, int code)> on_set_failed;
        // event_occured <siid> <eiid> [<piid> <value>]...
        std::function<void(uint16_t siid, uint16_t eiid, const std::vector<PropertyUpdate> &)> on_event;
        // The user asked for a network reset on the device itself.
        std::function<void()> on_restore;
        // Reply for `net`.
        std::function<NetState()> net_state;
        // Reply for `time` / `time posix`. Return "0" while the clock is unset.
        std::function<std::string(bool posix)> time_reply;
        // Reply for `mac`.
        std::function<std::string()> mac_reply;
        // Anything worth logging (MCU errors, unknown commands, parse problems).
        std::function<void(const std::string &)> log;
    };

    explicit Session(Hooks hooks);

    // Queue the commands every session starts with: announce the current
    // network state and ask the MCU for its firmware version.
    void start();

    // Process one line from the MCU (without the trailing '\r') and return the
    // reply to send back (without '\r'). `now_ms` is a monotonic timestamp.
    std::string handle_line(std::string_view line, uint32_t now_ms);

    void queue_set(PropertyId id, Value value) { m_queue.push_set(id, std::move(value)); }
    void queue_get(const std::vector<PropertyId> &ids);
    void queue_action(ActionId id, std::string args = "") { m_queue.push_action(id, std::move(args)); }

    // Tell the MCU about a changed network state right away (at the front of
    // the queue), if it differs from the last state announced.
    void announce_net_state();

    size_t queued() const { return m_queue.size(); }
    const std::string &model() const { return m_model; }
    const std::string &mcu_version() const { return m_mcu_version; }

    // Timestamp of the last line received; 0 before the first one.
    uint32_t last_rx_ms() const { return m_last_rx_ms; }
    bool mcu_seen() const { return m_seen; }

private:
    void handle_result(const std::vector<Token> &t);
    void handle_properties_changed(const std::vector<Token> &t);
    void handle_event(const std::vector<Token> &t);
    void log(const std::string &msg) const;

    Hooks m_hooks;
    CommandQueue m_queue;
    // What the last command handed over was, so its `result` can be parsed.
    Command::Kind m_pending = Command::Kind::Raw;
    bool m_has_pending = false;
    bool m_net_announced = false;
    NetState m_last_net = NetState::Offline;
    std::string m_model;
    std::string m_mcu_version;
    uint32_t m_last_rx_ms = 0;
    bool m_seen = false;
};

} // namespace miot
