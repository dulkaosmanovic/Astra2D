#include <astra2d/core/events.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

using astra2d::EventDispatcher;

namespace {

struct Hit {
    int damage = 0;
};
struct Message {
    std::string text;
};

} // namespace

TEST(EventTests, PublishCallsSubscribersOfThatTypeOnly)
{
    EventDispatcher events;
    int total = 0;
    int messages = 0;
    const auto hits = events.subscribe<Hit>([&](const Hit& hit) { total += hit.damage; });
    const auto texts = events.subscribe<Message>([&](const Message&) { ++messages; });
    events.publish(Hit{3});
    events.publish(Hit{4});
    EXPECT_EQ(total, 7);
    EXPECT_EQ(messages, 0);
}

TEST(EventTests, HandlersRunInSubscriptionOrder)
{
    EventDispatcher events;
    std::vector<int> order;
    const auto first = events.subscribe<Hit>([&](const Hit&) { order.push_back(1); });
    const auto second = events.subscribe<Hit>([&](const Hit&) { order.push_back(2); });
    events.publish(Hit{});
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST(EventTests, DestroyingTheSubscriptionUnsubscribes)
{
    EventDispatcher events;
    int calls = 0;
    {
        const auto subscription = events.subscribe<Hit>([&](const Hit&) { ++calls; });
        events.publish(Hit{});
        EXPECT_EQ(events.subscriberCount<Hit>(), 1U);
    }
    events.publish(Hit{});
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(events.subscriberCount<Hit>(), 0U);
}

TEST(EventTests, MovedSubscriptionKeepsTheHandler)
{
    EventDispatcher events;
    int calls = 0;
    EventDispatcher::Subscription kept;
    {
        auto subscription = events.subscribe<Hit>([&](const Hit&) { ++calls; });
        kept = std::move(subscription);
    }
    events.publish(Hit{});
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(kept.active());
    kept.reset();
    EXPECT_FALSE(kept.active());
    events.publish(Hit{});
    EXPECT_EQ(calls, 1);
}

TEST(EventTests, SubscriptionMayOutliveTheDispatcher)
{
    EventDispatcher::Subscription subscription;
    {
        EventDispatcher events;
        subscription = events.subscribe<Hit>([](const Hit&) {});
    }
    EXPECT_FALSE(subscription.active());
    subscription.reset(); // must not touch the destroyed dispatcher
}

TEST(EventTests, HandlerMayUnsubscribeItselfAndOthersDuringDispatch)
{
    EventDispatcher events;
    std::vector<int> calls;
    std::optional<EventDispatcher::Subscription> second;
    std::optional<EventDispatcher::Subscription> first;
    first = events.subscribe<Hit>([&](const Hit&) {
        calls.push_back(1);
        first.reset();  // itself
        second.reset(); // a later handler: must not run for this event
    });
    second = events.subscribe<Hit>([&](const Hit&) { calls.push_back(2); });
    events.publish(Hit{});
    events.publish(Hit{});
    EXPECT_EQ(calls, (std::vector<int>{1}));
    EXPECT_EQ(events.subscriberCount<Hit>(), 0U);
}

TEST(EventTests, HandlerAddedDuringDispatchStartsWithTheNextEvent)
{
    EventDispatcher events;
    int lateCalls = 0;
    std::vector<EventDispatcher::Subscription> added;
    const auto adder = events.subscribe<Hit>([&](const Hit&) {
        // Many subscriptions force the handler list to grow while this handler runs.
        for (int i = 0; i < 64; ++i) {
            added.push_back(events.subscribe<Hit>([&](const Hit&) { ++lateCalls; }));
        }
    });
    events.publish(Hit{});
    EXPECT_EQ(lateCalls, 0);
    added.erase(added.begin() + 1, added.end());
    events.publish(Hit{}); // adds 64 more, the one kept from before runs
    EXPECT_EQ(lateCalls, 1);
}

TEST(EventTests, QueuedEventsWaitForDispatchQueued)
{
    EventDispatcher events;
    std::vector<int> damage;
    const auto subscription =
        events.subscribe<Hit>([&](const Hit& hit) { damage.push_back(hit.damage); });
    events.enqueue(Hit{1});
    events.enqueue(Hit{2});
    EXPECT_TRUE(damage.empty());
    EXPECT_EQ(events.queuedCount(), 2U);
    events.dispatchQueued();
    EXPECT_EQ(damage, (std::vector<int>{1, 2}));
    EXPECT_EQ(events.queuedCount(), 0U);
}

TEST(EventTests, EventsQueuedByHandlersWaitForTheNextDispatch)
{
    EventDispatcher events;
    int hits = 0;
    const auto subscription = events.subscribe<Hit>([&](const Hit& hit) {
        ++hits;
        if (hit.damage > 0) {
            events.enqueue(Hit{hit.damage - 1}); // would loop forever if dispatched right away
        }
    });
    events.enqueue(Hit{2});
    events.dispatchQueued();
    EXPECT_EQ(hits, 1);
    events.dispatchQueued();
    events.dispatchQueued();
    EXPECT_EQ(hits, 3);
}

TEST(EventTests, QueuedEventIsACopy)
{
    EventDispatcher events;
    std::string received;
    const auto subscription =
        events.subscribe<Message>([&](const Message& message) { received = message.text; });
    {
        Message message{"hello"};
        events.enqueue(message);
        message.text = "changed";
    }
    events.dispatchQueued();
    EXPECT_EQ(received, "hello");
}
