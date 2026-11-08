#include "astra2d/core/events.hpp"

namespace astra2d {

void EventDispatcher::dispatch(std::type_index type, const void* event)
{
    if (const auto it = m_channels.find(type); it != m_channels.end()) {
        it->second.emit(event);
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
    const auto it = m_channels.find(type);
    return it != m_channels.end() ? it->second.size() : 0;
}

} // namespace astra2d
