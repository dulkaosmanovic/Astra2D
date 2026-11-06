#include <astra2d/core/assert.hpp>
#include <astra2d/ecs/entity.hpp>
#include <astra2d/ecs/registry.hpp>
#include <astra2d/ecs/signal.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using astra2d::Connection;
using astra2d::Entity;
using astra2d::Registry;
using astra2d::Signal;

namespace {

struct Position {
    float x = 0.0F;
    float y = 0.0F;
};

struct Velocity {
    float x = 0.0F;
    float y = 0.0F;
};

struct Tag {};

// Not default-constructible and move-only, to check that pools only need moves.
struct Resource {
    explicit Resource(int value)
        : handle(std::make_unique<int>(value))
    {
    }
    std::unique_ptr<int> handle;
};

std::vector<Entity> collect(auto view)
{
    std::vector<Entity> result;
    for (const Entity entity : view) {
        result.push_back(entity);
    }
    std::ranges::sort(result, [](Entity a, Entity b) { return a.value() < b.value(); });
    return result;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Entity

TEST(EntityTests, PacksIndexAndGeneration)
{
    const Entity entity = Entity::fromParts(1234, 56);
    EXPECT_EQ(entity.index(), 1234U);
    EXPECT_EQ(entity.generation(), 56U);
    EXPECT_EQ(Entity::fromValue(entity.value()), entity);
    EXPECT_NE(Entity::fromParts(1234, 57), entity);
}

TEST(EntityTests, DefaultIsNull)
{
    constexpr Entity entity;
    EXPECT_TRUE(entity.isNull());
    EXPECT_FALSE(static_cast<bool>(entity));
    EXPECT_EQ(entity, astra2d::kNullEntity);
    EXPECT_FALSE(Entity::fromParts(0, 0).isNull());
}

TEST(EntityTests, WorksAsHashKey)
{
    const std::unordered_set<Entity> set{Entity::fromParts(1, 0), Entity::fromParts(1, 1)};
    EXPECT_EQ(set.size(), 2U);
}

// ---------------------------------------------------------------------------------------------
// Entity lifetime

TEST(RegistryTests, CreatedEntitiesAreValidAndDistinct)
{
    Registry registry;
    const Entity a = registry.create();
    const Entity b = registry.create();
    EXPECT_NE(a, b);
    EXPECT_TRUE(registry.valid(a));
    EXPECT_TRUE(registry.valid(b));
    EXPECT_EQ(registry.alive(), 2U);
    EXPECT_FALSE(registry.valid(astra2d::kNullEntity));
}

TEST(RegistryTests, DestroyedHandleBecomesStaleWhenSlotIsReused)
{
    Registry registry;
    const Entity old = registry.create();
    registry.destroy(old);
    EXPECT_FALSE(registry.valid(old));

    const Entity reused = registry.create();
    EXPECT_EQ(reused.index(), old.index());               // the slot is recycled...
    EXPECT_EQ(reused.generation(), old.generation() + 1); // ...with a new generation
    EXPECT_TRUE(registry.valid(reused));
    EXPECT_FALSE(registry.valid(old)); // the old handle does not see the new entity
    EXPECT_EQ(registry.tryGet<Position>(old), nullptr);
    EXPECT_FALSE(registry.has<Position>(old));
}

TEST(RegistryTests, SlotIsRetiredBeforeTheGenerationWrapsAround)
{
    Registry registry;
    Entity entity = registry.create();
    const std::uint32_t index = entity.index();
    for (std::uint32_t i = 0; i < Entity::kGenerationMask; ++i) {
        ASSERT_EQ(entity.index(), index);
        registry.destroy(entity);
        entity = registry.create();
    }
    // The slot reached the last generation; destroying it again retires it.
    EXPECT_EQ(entity.generation(), Entity::kGenerationMask);
    registry.destroy(entity);
    const Entity next = registry.create();
    EXPECT_NE(next.index(), index);
    EXPECT_FALSE(registry.valid(Entity::fromParts(index, 0)));
}

TEST(RegistryTests, DestroyRemovesAllComponents)
{
    Registry registry;
    const Entity entity = registry.create();
    registry.emplace<Position>(entity);
    registry.emplace<Velocity>(entity);
    registry.destroy(entity);
    EXPECT_EQ(registry.count<Position>(), 0U);
    EXPECT_EQ(registry.count<Velocity>(), 0U);
    EXPECT_EQ(registry.alive(), 0U);
}

TEST(RegistryTests, ClearDestroysEverythingAndKeepsOldHandlesInvalid)
{
    Registry registry;
    const Entity a = registry.create();
    registry.emplace<Position>(a);
    registry.clear();
    EXPECT_EQ(registry.alive(), 0U);
    EXPECT_EQ(registry.count<Position>(), 0U);
    const Entity b = registry.create();
    EXPECT_EQ(b.index(), a.index());
    EXPECT_FALSE(registry.valid(a));
}

TEST(RegistryTests, ForEachVisitsLivingEntities)
{
    Registry registry;
    const Entity a = registry.create();
    const Entity b = registry.create();
    const Entity c = registry.create();
    registry.destroy(b);
    std::vector<Entity> visited;
    registry.forEach([&](Entity entity) { visited.push_back(entity); });
    EXPECT_EQ(visited, (std::vector<Entity>{a, c}));
}

// ---------------------------------------------------------------------------------------------
// Components

TEST(RegistryTests, EmplaceGetHasRemove)
{
    Registry registry;
    const Entity entity = registry.create();
    registry.emplace<Position>(entity, 1.0F, 2.0F);
    EXPECT_TRUE(registry.has<Position>(entity));
    EXPECT_FALSE((registry.has<Position, Velocity>(entity)));
    EXPECT_FLOAT_EQ(registry.get<Position>(entity).y, 2.0F);

    registry.get<Position>(entity).x = 5.0F;
    EXPECT_FLOAT_EQ(registry.get<Position>(entity).x, 5.0F);

    EXPECT_TRUE(registry.remove<Position>(entity));
    EXPECT_FALSE(registry.remove<Position>(entity));
    EXPECT_FALSE(registry.has<Position>(entity));
    EXPECT_EQ(registry.tryGet<Position>(entity), nullptr);
}

TEST(RegistryTests, GetAllReturnsReferences)
{
    Registry registry;
    const Entity entity = registry.create();
    registry.emplace<Position>(entity);
    registry.emplace<Velocity>(entity, 3.0F, 4.0F);
    auto [position, velocity] = registry.getAll<Position, Velocity>(entity);
    position.x = velocity.x;
    EXPECT_FLOAT_EQ(registry.get<Position>(entity).x, 3.0F);
}

TEST(RegistryTests, MoveOnlyComponentsWork)
{
    Registry registry;
    const Entity a = registry.create();
    const Entity b = registry.create();
    registry.emplace<Resource>(a, 1);
    registry.emplace<Resource>(b, 2);
    registry.remove<Resource>(a); // b moves into a's slot
    EXPECT_EQ(*registry.get<Resource>(b).handle, 2);
}

TEST(RegistryTests, EmplaceOrReplaceReplaces)
{
    Registry registry;
    const Entity entity = registry.create();
    registry.emplaceOrReplace<Position>(entity, 1.0F, 1.0F);
    registry.emplaceOrReplace<Position>(entity, 2.0F, 2.0F);
    EXPECT_EQ(registry.count<Position>(), 1U);
    EXPECT_FLOAT_EQ(registry.get<Position>(entity).x, 2.0F);
}

TEST(RegistryTests, RemovalKeepsOtherComponentsCorrect)
{
    // Swap-and-pop moves the last component into the gap; every entity must still find its own.
    Registry registry;
    std::vector<Entity> entities;
    for (int i = 0; i < 100; ++i) {
        const Entity entity = registry.create();
        registry.emplace<Position>(entity, static_cast<float>(i), 0.0F);
        entities.push_back(entity);
    }
    for (int i = 0; i < 100; i += 3) {
        registry.remove<Position>(entities[static_cast<std::size_t>(i)]);
    }
    for (int i = 0; i < 100; ++i) {
        const Entity entity = entities[static_cast<std::size_t>(i)];
        if (i % 3 == 0) {
            EXPECT_FALSE(registry.has<Position>(entity));
        } else {
            EXPECT_FLOAT_EQ(registry.get<Position>(entity).x, static_cast<float>(i));
        }
    }
}

TEST(RegistryTests, SparsePagesHandleHighIndices)
{
    Registry registry;
    Entity last;
    for (std::size_t i = 0; i < (2 * astra2d::SparseSet::kPageSize) + 10; ++i) {
        last = registry.create();
    }
    registry.emplace<Tag>(last); // only the third page is allocated
    EXPECT_TRUE(registry.has<Tag>(last));
    EXPECT_EQ(registry.count<Tag>(), 1U);
}

TEST(RegistryTests, ConstAccess)
{
    Registry registry;
    const Entity entity = registry.create();
    registry.emplace<Position>(entity, 7.0F, 0.0F);
    const Registry& view = registry;
    EXPECT_FLOAT_EQ(view.get<Position>(entity).x, 7.0F);
    EXPECT_NE(view.tryGet<Position>(entity), nullptr);
    EXPECT_EQ(view.tryGet<Velocity>(entity), nullptr);
}

#if ASTRA_ASSERTS_ENABLED
TEST(RegistryDeathTest, MisuseIsCaughtByAssertions)
{
    Registry registry;
    const Entity entity = registry.create();
    registry.emplace<Position>(entity);
    EXPECT_DEATH(registry.emplace<Position>(entity), "already has");
    EXPECT_DEATH((void)registry.get<Velocity>(entity), "does not have");
    registry.destroy(entity);
    EXPECT_DEATH(registry.destroy(entity), "invalid entity");
    EXPECT_DEATH(registry.emplace<Velocity>(entity), "invalid entity");
}
#endif

// ---------------------------------------------------------------------------------------------
// Views

TEST(ViewTests, VisitsExactlyTheEntitiesWithAllComponents)
{
    Registry registry;
    const Entity both = registry.create();
    const Entity positionOnly = registry.create();
    const Entity velocityOnly = registry.create();
    registry.emplace<Position>(both);
    registry.emplace<Velocity>(both);
    registry.emplace<Position>(positionOnly);
    registry.emplace<Velocity>(velocityOnly);

    EXPECT_EQ(collect(registry.view<Position, Velocity>()), std::vector<Entity>{both});
    EXPECT_EQ(collect(registry.view<Position>()), (std::vector<Entity>{both, positionOnly}));
    EXPECT_EQ((registry.view<Position, Velocity>().count()), 1U);
    EXPECT_TRUE(registry.view<Position>().contains(positionOnly));
    EXPECT_FALSE((registry.view<Position, Velocity>().contains(positionOnly)));
}

TEST(ViewTests, EachPassesComponentsWithOrWithoutTheEntity)
{
    Registry registry;
    const Entity entity = registry.create();
    registry.emplace<Position>(entity, 1.0F, 1.0F);
    registry.emplace<Velocity>(entity, 2.0F, 3.0F);

    registry.view<Position, Velocity>().each([](Position& p, const Velocity& v) {
        p.x += v.x;
        p.y += v.y;
    });
    Entity seen;
    registry.view<Position>().each([&](Entity e, Position&) { seen = e; });

    EXPECT_FLOAT_EQ(registry.get<Position>(entity).x, 3.0F);
    EXPECT_FLOAT_EQ(registry.get<Position>(entity).y, 4.0F);
    EXPECT_EQ(seen, entity);
}

TEST(ViewTests, IteratesTheSmallestPool)
{
    Registry registry;
    for (int i = 0; i < 1000; ++i) {
        registry.emplace<Position>(registry.create());
    }
    const Entity tagged = registry.create();
    registry.emplace<Position>(tagged);
    registry.emplace<Tag>(tagged);

    const auto view = registry.view<Position, Tag>();
    EXPECT_EQ(view.sizeHint(), 1U); // the Tag pool leads, not the 1001 positions
    EXPECT_EQ(collect(view), std::vector<Entity>{tagged});
}

TEST(ViewTests, MissingPoolGivesEmptyView)
{
    Registry registry;
    registry.emplace<Position>(registry.create());
    int calls = 0;
    registry.view<Position, Velocity>().each([&](Position&, Velocity&) { ++calls; });
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(registry.view<Tag>().begin(), registry.view<Tag>().end());
}

TEST(ViewTests, DestroyingTheVisitedEntityDuringIterationIsSafe)
{
    Registry registry;
    for (int i = 0; i < 10; ++i) {
        const Entity entity = registry.create();
        registry.emplace<Position>(entity, static_cast<float>(i), 0.0F);
    }
    int visited = 0;
    registry.view<Position>().each([&](Entity entity, Position& position) {
        ++visited;
        if (static_cast<int>(position.x) % 2 == 0) {
            registry.destroy(entity);
        }
    });
    EXPECT_EQ(visited, 10); // nobody skipped, nobody visited twice
    EXPECT_EQ(registry.count<Position>(), 5U);
    registry.view<Position>().each(
        [](const Position& p) { EXPECT_EQ(static_cast<int>(p.x) % 2, 1); });
}

TEST(ViewTests, RemovingComponentsOfTheVisitedEntityDuringIterationIsSafe)
{
    Registry registry;
    for (int i = 0; i < 10; ++i) {
        const Entity entity = registry.create();
        registry.emplace<Position>(entity);
        registry.emplace<Velocity>(entity);
    }
    int visited = 0;
    registry.view<Position, Velocity>().each([&](Entity entity, Position&, Velocity&) {
        ++visited;
        registry.remove<Velocity>(entity); // a non-leading pool
        registry.remove<Position>(entity);
    });
    EXPECT_EQ(visited, 10);
    EXPECT_EQ(registry.count<Position>(), 0U);
}

TEST(ViewTests, AddingOtherComponentsDuringIterationIsSafe)
{
    Registry registry;
    for (int i = 0; i < 10; ++i) {
        registry.emplace<Position>(registry.create());
    }
    int visited = 0;
    registry.view<Position>().each([&](Entity entity, Position&) {
        ++visited;
        registry.emplace<Velocity>(entity);
        (void)registry.create(); // created entities are not visited
    });
    EXPECT_EQ(visited, 10);
    EXPECT_EQ(registry.count<Velocity>(), 10U);
}

TEST(ViewTests, RangeForMatchesEach)
{
    Registry registry;
    std::vector<Entity> expected;
    for (int i = 0; i < 20; ++i) {
        const Entity entity = registry.create();
        registry.emplace<Position>(entity);
        if (i % 4 == 0) {
            registry.emplace<Tag>(entity);
            expected.push_back(entity);
        }
    }
    auto view = registry.view<Tag, Position>();
    std::vector<Entity> fromEach;
    view.each([&](Entity e, Tag&, Position&) { fromEach.push_back(e); });
    std::vector<Entity> fromRange;
    for (const Entity e : view) {
        fromRange.push_back(e);
        (void)view.get<Position>(e);
    }
    EXPECT_EQ(fromEach, fromRange);
    EXPECT_EQ(collect(view), expected);
}

// ---------------------------------------------------------------------------------------------
// Signals

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

TEST(RegistrySignalTests, ConstructAndDestroyAreReported)
{
    Registry registry;
    std::vector<std::string> log;
    const Connection constructed = registry.onConstruct<Position>().connect(
        [&](Registry& r, Entity e) { log.push_back("+" + std::to_string(r.get<Position>(e).x)); });
    const Connection destroyed = registry.onDestroy<Position>().connect([&](Registry& r, Entity e) {
        // The component can still be read while it is being removed.
        log.push_back("-" + std::to_string(r.get<Position>(e).x));
    });

    const Entity a = registry.create();
    registry.emplace<Position>(a, 1.0F, 0.0F);
    const Entity b = registry.create();
    registry.emplace<Position>(b, 2.0F, 0.0F);
    registry.remove<Position>(a);
    registry.destroy(b); // destroying the entity reports its components too

    ASSERT_EQ(log.size(), 4U);
    EXPECT_EQ(log[0].substr(0, 2), "+1");
    EXPECT_EQ(log[1].substr(0, 2), "+2");
    EXPECT_EQ(log[2].substr(0, 2), "-1");
    EXPECT_EQ(log[3].substr(0, 2), "-2");
}

TEST(RegistrySignalTests, HandlerMayAddComponentsToTheNewEntity)
{
    // A common use: a "requires" relation, e.g. every Velocity gets a Position.
    Registry registry;
    const Connection requires_ =
        registry.onConstruct<Velocity>().connect([](Registry& r, Entity e) {
        if (!r.has<Position>(e)) {
            r.emplace<Position>(e);
        }
    });
    const Entity entity = registry.create();
    const auto& velocity = registry.emplace<Velocity>(entity, 1.0F, 2.0F);
    EXPECT_TRUE(registry.has<Position>(entity));
    EXPECT_FLOAT_EQ(velocity.y, 2.0F);
}

TEST(RegistrySignalTests, ReplaceReportsDestroyThenConstruct)
{
    Registry registry;
    std::string log;
    const Connection c1 =
        registry.onConstruct<Position>().connect([&](Registry&, Entity) { log += '+'; });
    const Connection c2 =
        registry.onDestroy<Position>().connect([&](Registry&, Entity) { log += '-'; });
    const Entity entity = registry.create();
    registry.emplaceOrReplace<Position>(entity);
    registry.emplaceOrReplace<Position>(entity);
    EXPECT_EQ(log, "+-+");
}

TEST(ViewTests, PairsTheRightComponentsWhenPoolsAreInDifferentOrders)
{
    // The view takes a shortcut when an entity sits at the same position in every pool; here
    // the orders differ (Velocity added in reverse, some removed), so it must fall back to lookups.
    Registry registry;
    std::vector<Entity> entities;
    for (int i = 0; i < 50; ++i) {
        const Entity entity = registry.create();
        registry.emplace<Position>(entity, static_cast<float>(i), 0.0F);
        entities.push_back(entity);
    }
    for (int i = 49; i >= 0; --i) {
        registry.emplace<Velocity>(entities[static_cast<std::size_t>(i)], static_cast<float>(i),
                                   0.0F);
    }
    for (int i = 0; i < 50; i += 7) {
        registry.remove<Position>(entities[static_cast<std::size_t>(i)]);
    }
    int visited = 0;
    registry.view<Position, Velocity>().each([&](const Position& p, const Velocity& v) {
        EXPECT_FLOAT_EQ(p.x, v.x);
        ++visited;
    });
    registry.view<Velocity, Position>().each(
        [&](const Velocity& v, const Position& p) { EXPECT_FLOAT_EQ(p.x, v.x); });
    EXPECT_EQ(visited, 50 - 8);
}

// ---------------------------------------------------------------------------------------------
// Handlers that change the entity or pool being processed (code review regressions)

namespace {
// Types used nowhere else, so their pools only come into existence inside the handler.
template <int N>
struct FirstUseInHandler {};
struct Doomed {};

template <int... N>
void emplaceNewTypes(Registry& registry, Entity entity, std::integer_sequence<int, N...> /*types*/)
{
    (registry.emplace<FirstUseInHandler<N>>(entity), ...);
}
} // namespace

TEST(RegistrySignalTests, DestroyHandlerMayUseNewComponentTypes)
{
    // The first handler that runs creates 32 new pools while destroy() is still walking the pool
    // list, which reallocates it. Walking it by iterator then read freed memory (ASan reports it).
    Registry registry;
    bool spawned = false;
    const auto spawn = [&](Registry& r, Entity) {
        if (!spawned) {
            spawned = true;
            emplaceNewTypes(r, r.create(), std::make_integer_sequence<int, 32>{});
        }
    };
    // Both pools get the handler, so it runs first whichever order destroy() visits them in, and
    // the other pool is visited after the reallocation.
    const Connection a = registry.onDestroy<Position>().connect(spawn);
    const Connection b = registry.onDestroy<Velocity>().connect(spawn);
    const Entity entity = registry.create();
    registry.emplace<Position>(entity);
    registry.emplace<Velocity>(entity);
    registry.destroy(entity);
    EXPECT_FALSE(registry.valid(entity));
    EXPECT_EQ(registry.count<Position>(), 0U);
    EXPECT_EQ(registry.count<Velocity>(), 0U);
    EXPECT_EQ(registry.count<FirstUseInHandler<31>>(), 1U);
}

TEST(RegistrySignalTests, ConstructHandlerRemovingTheComponentMakesEmplaceThrow)
{
    // A "reject" handler: emplace() has no component to return any more.
    Registry registry;
    const Connection reject = registry.onConstruct<Position>().connect(
        [](Registry& r, Entity e) { r.remove<Position>(e); });
    const Entity entity = registry.create();
    EXPECT_THROW((void)registry.emplace<Position>(entity), std::logic_error);
    EXPECT_FALSE(registry.has<Position>(entity));
}

TEST(RegistrySignalTests, DestroyHandlerMayDestroyTheSameEntityAgain)
{
    Registry registry;
    const Connection again =
        registry.onDestroy<Position>().connect([](Registry& r, Entity e) { r.destroy(e); });
    const Entity entity = registry.create();
    registry.emplace<Position>(entity);
    registry.emplace<Velocity>(entity);
    registry.destroy(entity);
    EXPECT_FALSE(registry.valid(entity));
    EXPECT_EQ(registry.alive(), 0U);
    EXPECT_EQ(registry.count<Velocity>(), 0U);
}

#if ASTRA_ASSERTS_ENABLED
TEST(RegistrySignalDeathTest, AddingComponentsToAnEntityBeingDestroyedAsserts)
{
    Registry registry;
    const Connection resurrect =
        registry.onDestroy<Position>().connect([](Registry& r, Entity e) { r.emplace<Doomed>(e); });
    const Entity entity = registry.create();
    registry.emplace<Position>(entity);
    EXPECT_DEATH(registry.destroy(entity), "being destroyed");
}
#else
TEST(RegistrySignalTests, ComponentAddedToAnEntityBeingDestroyedIsDropped)
{
    // Without assertions the component is discarded, so no stale entry survives the slot reuse.
    Registry registry;
    const Connection resurrect =
        registry.onDestroy<Position>().connect([](Registry& r, Entity e) { r.emplace<Doomed>(e); });
    const Entity entity = registry.create();
    registry.emplace<Position>(entity);
    registry.destroy(entity);
    EXPECT_EQ(registry.count<Doomed>(), 0U);
    const Entity reused = registry.create();
    EXPECT_EQ(reused.index(), entity.index());
    EXPECT_EQ(registry.view<Doomed>().count(), 0U);
}
#endif

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
