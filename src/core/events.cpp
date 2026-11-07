#include "astra2d/core/events.hpp"

#include <algorithm>

namespace astra2d {

namespace {

struct Handler {
    std::uint64_t id;
    std::function<void(const void*)> callback;
    bool alive = true;
};

// Handlers of one event type. Handlers are heap-allocated, so a handler that subscribes another
// one (growing the vector) does not move the handler that is running.
struct Channel {
    std::vector<std::unique_ptr<Handler>> handlers;
    int dispatchDepth = 0; // > 0 while handlers of this channel are running
    bool hasDead = false;

    void removeDead()
    {
        std::erase_if(handlers, [](const auto& handler) { return !handler->alive; });
        hasDead = false;
    }
};

} // namespace

// Shared with the Subscriptions through weak pointers, so they notice when the dispatcher is gone.
struct EventDispatcher::State {
    std::unordered_map<std::type_index, Channel> channels;
    std::uint64_t nextId = 1;

    void remove(std::type_index type, std::uint64_t id)
    {
        const auto it = channels.find(type);
        if (it == channels.end()) {
            return;
        }
        Channel& channel = it->second;
        for (const auto& handler : channel.handlers) {
            if (handler->id == id) {
                handler->alive = false;
                channel.hasDead = true;
            }
        }
        // Removing while handlers run would invalidate the loop in dispatch(); it cleans up later.
        if (channel.dispatchDepth == 0) {
            channel.removeDead();
        }
    }
};

void EventDispatcher::Subscription::reset() noexcept
{
    if (m_id == 0) {
        return;
    }
    if (const auto state = m_state.lock()) {
        state->remove(m_type, m_id);
    }
    m_state.reset();
    m_id = 0;
}

EventDispatcher::EventDispatcher()
    : m_state(std::make_shared<State>())
{
}

EventDispatcher::~EventDispatcher() = default;

std::uint64_t EventDispatcher::addHandler(std::type_index type, Callback callback)
{
    const std::uint64_t id = m_state->nextId++;
    m_state->channels[type].handlers.push_back(
        std::make_unique<Handler>(Handler{.id = id, .callback = std::move(callback)}));
    return id;
}

void EventDispatcher::dispatch(std::type_index type, const void* event)
{
    const auto it = m_state->channels.find(type);
    if (it == m_state->channels.end()) {
        return;
    }
    Channel& channel = it->second;
    // Handlers added by a handler are appended; only those present now receive this event.
    const std::size_t count = channel.handlers.size();
    ++channel.dispatchDepth;
    try {
        for (std::size_t i = 0; i < count; ++i) {
            const Handler& handler = *channel.handlers[i];
            if (handler.alive) {
                handler.callback(event);
            }
        }
    } catch (...) {
        // Else the depth stays above 0 and unsubscribed handlers are never erased again.
        --channel.dispatchDepth;
        throw;
    }
    --channel.dispatchDepth;
    if (channel.dispatchDepth == 0 && channel.hasDead) {
        channel.removeDead();
    }
}

void EventDispatcher::dispatchQueued()
{
    // Swap first: events queued by handlers wait for the next call instead of looping forever.
    std::vector<std::function<void()>> queue;
    queue.swap(m_queue);
    for (const auto& publishEvent : queue) {
        publishEvent();
    }
}

std::size_t EventDispatcher::handlerCount(std::type_index type) const
{
    const auto it = m_state->channels.find(type);
    if (it == m_state->channels.end()) {
        return 0;
    }
    return static_cast<std::size_t>(std::ranges::count_if(
        it->second.handlers, [](const auto& handler) { return handler->alive; }));
}

} // namespace astra2d
