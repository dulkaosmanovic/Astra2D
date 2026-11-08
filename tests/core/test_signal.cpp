#include <astra2d/core/signal.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

using astra2d::Connection;
using astra2d::Signal;

TEST(SignalTests, ConnectEmitDisconnect)
{
    Signal<int> signal;
    int sum = 0;
    Connection connection = signal.connect([&](int value) { sum += value; });
    signal.emit(2);
    EXPECT_EQ(sum, 2);
    EXPECT_TRUE(connection.connected());
    connection.reset();
    signal.emit(5);
    EXPECT_EQ(sum, 2);
    EXPECT_TRUE(signal.empty());
}

TEST(SignalTests, ConnectionDisconnectsWhenDestroyed)
{
    Signal<> signal;
    int calls = 0;
    {
        const Connection connection = signal.connect([&] { ++calls; });
        signal.emit();
    }
    signal.emit();
    EXPECT_EQ(calls, 1);
}

TEST(SignalTests, HandlerMayDisconnectItselfAndConnectOthers)
{
    Signal<> signal;
    std::vector<std::string> log;
    Connection self;
    Connection added;
    self = signal.connect([&] {
        log.emplace_back("self");
        self.reset();
        added = signal.connect([&] { log.emplace_back("added"); });
    });
    signal.emit(); // "added" is connected during this emit, so it first runs next time
    signal.emit();
    EXPECT_EQ(log, (std::vector<std::string>{"self", "added"}));
}

TEST(SignalTests, ConnectionMayOutliveTheSignal)
{
    Connection connection;
    {
        Signal<> signal;
        connection = signal.connect([] {});
    }
    EXPECT_FALSE(connection.connected());
    connection.reset(); // no crash
}

TEST(SignalTests, NestedEmitAndDisconnectDuringEmit)
{
    // emit() no longer copies the handler list; nested emits must still see stable indices.
    Signal<int> signal;
    std::vector<int> log;
    Connection second;
    const Connection first = signal.connect([&](int depth) {
        log.push_back(depth);
        if (depth == 0) {
            signal.emit(1); // nested
            second.reset(); // disconnect a handler that has not run yet in the outer emit
        }
    });
    second = signal.connect([&](int depth) { log.push_back(100 + depth); });
    signal.emit(0);
    // outer: first(0) -> nested: first(1), second(1) -> second disconnected before outer reaches it
    EXPECT_EQ(log, (std::vector<int>{0, 1, 101}));
    EXPECT_EQ(signal.size(), 1U);
}

TEST(SignalTests, ThrowingHandlerLeavesTheSignalUsable)
{
    Signal<> signal;
    int calls = 0;
    Connection throwing = signal.connect([] { throw std::runtime_error("handler failed"); });
    EXPECT_THROW(signal.emit(), std::runtime_error);
    throwing.reset();
    const Connection counting = signal.connect([&] { ++calls; });
    signal.emit(); // the disconnected handler is cleaned up and not called
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(signal.size(), 1U);
}
