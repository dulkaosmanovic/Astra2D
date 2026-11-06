#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

/// @addtogroup ecs
/// @{

namespace astra2d {

/// Keeps a Signal handler connected; disconnects it when destroyed or reset(). Move-only, may
/// outlive the signal.
class [[nodiscard]] Connection {
public:
    /// An empty connection.
    Connection() = default;
    ~Connection() { reset(); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    /// Takes over the handler of @p other, which becomes empty.
    Connection(Connection&& other) noexcept
        : m_alive(std::move(other.m_alive))
    {
    }
    /// Disconnects the current handler and takes over the one of @p other.
    Connection& operator=(Connection&& other) noexcept
    {
        if (this != &other) {
            reset();
            m_alive = std::move(other.m_alive);
        }
        return *this;
    }

    /// Disconnects the handler now. Safe to call more than once and after the signal is gone.
    void reset() noexcept
    {
        if (const auto alive = m_alive.lock()) {
            *alive = false;
        }
        m_alive.reset();
    }

    /// True while the handler is connected to a living signal.
    [[nodiscard]] bool connected() const noexcept
    {
        const auto alive = m_alive.lock();
        return alive && *alive;
    }

private:
    template <typename... Args>
    friend class Signal;

    explicit Connection(std::weak_ptr<bool> alive)
        : m_alive(std::move(alive))
    {
    }

    std::weak_ptr<bool> m_alive;
};

/// A list of handlers called with the same arguments, e.g. the Registry's "component added" and
/// "component removed" notifications.
///
/// @code
/// Connection connection = signal.connect([](Registry& registry, Entity entity) { ... });
/// @endcode
///
/// Handlers run in connection order. A handler may connect or disconnect handlers (also itself)
/// while the signal is being emitted: new ones are first called by the next emit(), disconnected
/// ones are not called anymore. Not thread-safe.
template <typename... Args>
class Signal {
public:
    Signal() = default;
    ~Signal() = default;
    // Connections point at the handlers' flags, not at the signal, so moving it is safe; copying
    // would share handlers between two signals, so it is not allowed.
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    /// Takes over the handlers of @p other; their Connections stay valid.
    Signal(Signal&& other) noexcept = default;
    /// Replaces the handlers with those of @p other; their Connections stay valid.
    Signal& operator=(Signal&& other) noexcept = default;

    /// Adds @p handler, callable with `Args...`. It stays connected while the Connection lives.
    template <typename Handler>
    Connection connect(Handler&& handler)
    {
        const auto alive = std::make_shared<bool>(true);
        m_slots.push_back(
            std::make_shared<Slot>(Slot{.alive = alive, .call = std::forward<Handler>(handler)}));
        return Connection{alive};
    }

    /// Calls every connected handler.
    void emit(Args... args)
    {
        if (m_slots.empty()) {
            return;
        }
        // A snapshot: handlers that connect or disconnect others do not disturb this loop, and
        // the shared_ptr keeps a running handler alive even if it is removed meanwhile.
        const std::vector<std::shared_ptr<Slot>> slots = m_slots;
        bool anyDead = false;
        for (const auto& slot : slots) {
            if (*slot->alive) {
                slot->call(args...);
            }
        }
        for (const auto& slot : m_slots) {
            anyDead = anyDead || !*slot->alive;
        }
        if (anyDead) {
            std::erase_if(m_slots, [](const auto& slot) { return !*slot->alive; });
        }
    }

    /// Number of connected handlers.
    [[nodiscard]] std::size_t size() const
    {
        std::size_t count = 0;
        for (const auto& slot : m_slots) {
            count += *slot->alive ? 1U : 0U;
        }
        return count;
    }

    /// True if no handler is connected.
    [[nodiscard]] bool empty() const { return size() == 0; }

private:
    struct Slot {
        std::shared_ptr<bool> alive;
        std::function<void(Args...)> call;
    };

    std::vector<std::shared_ptr<Slot>> m_slots;
};

} // namespace astra2d

/// @}
