#pragma once

#include "astra2d/core/signal.hpp"

#include <cstddef>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

/// @addtogroup core
/// @{

namespace astra2d {

/// Sent when the framebuffer size changes (in pixels), after the viewport was updated.
struct WindowResizeEvent {
    int width = 0;  ///< New framebuffer width.
    int height = 0; ///< New framebuffer height.
};

/// Sent when the window gains or loses keyboard focus.
struct WindowFocusEvent {
    bool focused = false; ///< True if the window now has focus.
};

/// Sent when a gamepad is connected or disconnected.
struct GamepadConnectionEvent {
    int gamepad = 0;        ///< Gamepad index, 0 to Input::kMaxGamepads - 1.
    bool connected = false; ///< True on connection, false on disconnection.
};

/// Type-safe publish/subscribe for events of any type.
///
/// Systems that should not know each other communicate through events: the physics publishes a
/// `CollisionEvent`, the audio and the score subscribe to it.
///
/// @code
/// struct CoinCollected { int value; };
///
/// EventDispatcher events;
/// auto subscription = events.subscribe<CoinCollected>([&](const CoinCollected& e) {
///     m_score += e.value;
/// });
/// events.publish(CoinCollected{10});   // handlers run now
/// events.enqueue(CoinCollected{5});    // handlers run in the next dispatchQueued()
/// @endcode
///
/// - A handler stays registered while its Subscription exists; destroying or reset()ing the
///   Subscription removes it. A Subscription may outlive the dispatcher.
/// - Handlers run in subscription order. A handler may subscribe, unsubscribe or publish; handlers
///   added during a dispatch are first called for the next event.
/// - Event types are plain structs; no base class or registration is needed.
///
/// Not thread-safe: use it from one thread (the main thread).
class EventDispatcher {
public:
    /// Keeps a handler registered. Move-only; unsubscribes when destroyed or reset(), and may
    /// outlive the dispatcher. connected() is true while the handler is registered.
    using Subscription = Connection;

    EventDispatcher() = default;
    ~EventDispatcher() = default;
    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;
    EventDispatcher(EventDispatcher&&) = delete;
    EventDispatcher& operator=(EventDispatcher&&) = delete;

    /// Registers @p handler, callable as `handler(const Event&)`, for events of type @p Event.
    template <typename Event, typename Handler>
    [[nodiscard]] Subscription subscribe(Handler&& handler)
    {
        return m_channels[typeid(Event)].connect(
            [callback = std::forward<Handler>(handler)](const void* event) {
            callback(*static_cast<const Event*>(event));
        });
    }

    /// Calls every handler of @p Event now, before returning.
    template <typename Event>
    void publish(const Event& event)
    {
        dispatch(typeid(Event), &event);
    }

    /// Stores a copy of @p event; its handlers run in the next dispatchQueued(). Use it from code
    /// that must not run game logic right now (e.g. inside an event callback or a physics step).
    template <typename Event>
    void enqueue(Event event)
    {
        m_queue.emplace_back([this, stored = std::move(event)] { publish(stored); });
    }

    /// Publishes all queued events in the order they were queued. Events queued by their handlers
    /// wait for the next call. Application::run() calls this once per frame after polling events.
    void dispatchQueued();

    /// Number of queued events.
    [[nodiscard]] std::size_t queuedCount() const { return m_queue.size(); }

    /// Number of handlers registered for @p Event.
    template <typename Event>
    [[nodiscard]] std::size_t subscriberCount() const
    {
        return handlerCount(typeid(Event));
    }

private:
    void dispatch(std::type_index type, const void* event);
    [[nodiscard]] std::size_t handlerCount(std::type_index type) const;

    // One signal per event type. Map nodes never move, so a handler that subscribes to a new type
    // while its own channel emits does not move that channel.
    std::unordered_map<std::type_index, Signal<const void*>> m_channels;
    std::vector<std::function<void()>> m_queue;
};

} // namespace astra2d

/// @}
