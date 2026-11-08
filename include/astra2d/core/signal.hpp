#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

/// @addtogroup core
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

/// A list of handlers called with the same arguments: each event type of an EventDispatcher, and
/// the Registry's "component added" and "component removed" notifications.
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
        if (m_emitting == 0) { // else erased after the emit
            std::erase_if(m_slots, [](const auto& slot) { return !*slot->alive; });
        }
        const auto alive = std::make_shared<bool>(true);
        m_slots.push_back(
            std::make_unique<Slot>(Slot{.alive = alive, .call = std::forward<Handler>(handler)}));
        return Connection{alive};
    }

    /// Calls every connected handler.
    void emit(Args... args)
    {
        if (m_slots.empty()) {
            return;
        }
        // No copy of the handler list (emit runs on every component add/remove): by index, up to
        // the handlers present now, so handlers connected meanwhile wait for the next emit. Slots
        // are heap objects, so they stay put when the vector grows, and disconnected ones are only
        // erased once no emit is running, so indices stay valid in nested emits too.
        const std::size_t count = m_slots.size();
        {
            const EmitGuard guard(m_emitting);
            for (std::size_t i = 0; i < count; ++i) {
                const Slot& slot = *m_slots[i];
                if (*slot.alive) {
                    slot.call(args...);
                }
            }
        }
        if (m_emitting == 0) {
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

    // Counts running emits, also when a handler throws.
    class EmitGuard {
    public:
        explicit EmitGuard(int& depth)
            : m_depth(&depth)
        {
            ++*m_depth;
        }
        ~EmitGuard() { --*m_depth; }
        EmitGuard(const EmitGuard&) = delete;
        EmitGuard& operator=(const EmitGuard&) = delete;
        EmitGuard(EmitGuard&&) = delete;
        EmitGuard& operator=(EmitGuard&&) = delete;

    private:
        int* m_depth;
    };

    std::vector<std::unique_ptr<Slot>> m_slots;
    int m_emitting = 0;
};

} // namespace astra2d

/// @}
