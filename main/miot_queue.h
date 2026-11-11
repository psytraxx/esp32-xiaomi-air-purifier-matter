#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Commands waiting for the MCU's next `get_down` poll.
//
// The MCU is the protocol master: the module can only hand over one command
// per `get_down`. Commands therefore queue up here in FIFO order, with two
// refinements:
//   - a set for a property that already has a set waiting replaces the
//     waiting value in place (the MCU only needs the latest target, and the
//     order relative to other properties is kept);
//   - an identical get that is already waiting is not queued twice.
// The queue is bounded; when full, the oldest get (a poll, cheap to repeat) is
// dropped first, otherwise the oldest command.
//
// Pure C++ — unit-tested on the host.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "miot_protocol.h"

namespace miot
{

struct Command
{
    enum class Kind : uint8_t
    {
        Get,    // get_properties <siid> <piid> ...
        Set,    // set_properties <siid> <piid> <value>
        Action, // action <siid> <aiid> [args]
        Raw,    // verbatim, e.g. "MIIO_net_change cloud"
    };

    Kind kind = Kind::Raw;
    std::vector<PropertyId> gets; // Get
    PropertyId set_id;            // Set
    Value set_value;              // Set
    ActionId action;              // Action
    std::string text;             // Action arguments, or the Raw command

    // The command as sent after "down ".
    std::string render() const;
};

class CommandQueue
{
public:
    explicit CommandQueue(size_t capacity = 32) : m_capacity(capacity) {}

    void push_set(PropertyId id, Value value);
    void push_get(std::vector<PropertyId> ids);
    void push_action(ActionId id, std::string args);
    // `front` puts the command ahead of everything else (network-state
    // announcements, which the MCU should see before queued work).
    void push_raw(std::string text, bool front = false);

    std::optional<Command> pop();

    size_t size() const { return m_items.size(); }
    bool empty() const { return m_items.empty(); }
    void clear() { m_items.clear(); }

    // Total number of commands dropped because the queue was full.
    size_t dropped() const { return m_dropped; }

private:
    void make_room();
    void push_back(Command c);

    std::deque<Command> m_items;
    size_t m_capacity;
    size_t m_dropped = 0;
};

// Build get_properties commands for `ids`, split so that no command carries
// more than `max_props` properties (the spec allows 64 parameters, i.e. 32
// siid/piid pairs) or exceeds MAX_LINE_LENGTH once prefixed with "down ".
std::vector<std::vector<PropertyId>> chunk_gets(const std::vector<PropertyId> &ids, size_t max_props = 32);

} // namespace miot
