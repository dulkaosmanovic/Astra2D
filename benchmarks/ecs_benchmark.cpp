// Evaluation E2: "Is a sparse-set ECS faster than OOP game objects?"
//
// Updates N moving objects (position += velocity * dt) many times and reports the time per
// update, for three layouts of the same data:
//
//   ecs           Registry with Position, Velocity, Appearance and Health components; the update
//                 is a View<Position, Velocity>, so it reads two packed arrays and nothing else.
//   oop           One heap-allocated GameObject per object (all four parts as members) behind a
//                 base-class pointer, updated through a virtual update(). Objects are allocated
//                 one after another, so they usually lie in memory in update order: the best case
//                 for this layout.
//   oop_shuffled  The same objects, updated in a random order: what happens to a long-running
//                 game whose objects were created and destroyed over time, so that the update
//                 order no longer matches the memory order.
//
// Both layouts hold the same data per object; only its layout and the dispatch differ.
//
//   ecs_benchmark --approach ecs --entities 100000 [--iterations 200] [--warmup 20] [--header]
//
// Prints one CSV line to stdout:
//   approach,entities,iterations,mean_ms,stddev_ms,min_ms,max_ms,ns_per_entity
// and a checksum to stderr (it depends on every result, so the compiler cannot skip the work).

#include <astra2d/ecs/registry.hpp>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr float kDeltaTime = 1.0F / 60.0F;

// ---------------------------------------------------------------------------------------------
// The data of one object, split into components

struct Position {
    glm::vec2 value;
};
struct Velocity {
    glm::vec2 value;
};
// Data the update does not touch, as a real object would have (rendering, gameplay).
struct Appearance {
    glm::vec4 tint{1.0F};
    glm::vec2 size{16.0F};
    int layer = 0;
    int frame = 0;
};
struct Health {
    int current = 100;
    int maximum = 100;
};

// ---------------------------------------------------------------------------------------------
// The same data as a classic game object hierarchy

class GameObject {
public:
    GameObject() = default;
    virtual ~GameObject() = default;
    GameObject(const GameObject&) = delete;
    GameObject& operator=(const GameObject&) = delete;
    GameObject(GameObject&&) = delete;
    GameObject& operator=(GameObject&&) = delete;

    virtual void update(float deltaTime) = 0;
    [[nodiscard]] virtual glm::vec2 position() const = 0;
};

class Mover final : public GameObject {
public:
    Mover(glm::vec2 position, glm::vec2 velocity)
        : m_position(position)
        , m_velocity(velocity)
    {
    }
    void update(float deltaTime) override { m_position += m_velocity * deltaTime; }
    [[nodiscard]] glm::vec2 position() const override { return m_position; }

private:
    glm::vec2 m_position;
    glm::vec2 m_velocity;
    Appearance m_appearance; // carried along, as in a real object, but not updated
    Health m_health;
};

// ---------------------------------------------------------------------------------------------

struct Options {
    std::string approach = "ecs";
    int entities = 10'000;
    int iterations = 200;
    int warmup = 20;
    bool header = false;
};

int parseInt(std::string_view text, const char* name)
{
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value <= 0) {
        throw std::invalid_argument(std::string(name) + " needs a positive number");
    }
    return value;
}

Options parseOptions(std::span<char*> args)
{
    Options options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto next = [&]() -> std::string_view {
            if (i + 1 >= args.size()) {
                throw std::invalid_argument(std::string(arg) + " needs a value");
            }
            return args[++i];
        };
        if (arg == "--approach") {
            options.approach = next();
        } else if (arg == "--entities") {
            options.entities = parseInt(next(), "--entities");
        } else if (arg == "--iterations") {
            options.iterations = parseInt(next(), "--iterations");
        } else if (arg == "--warmup") {
            options.warmup = parseInt(next(), "--warmup");
        } else if (arg == "--header") {
            options.header = true;
        } else {
            throw std::invalid_argument("unknown argument " + std::string(arg));
        }
    }
    if (options.approach != "ecs" && options.approach != "oop" &&
        options.approach != "oop_shuffled") {
        throw std::invalid_argument("--approach must be ecs, oop or oop_shuffled");
    }
    return options;
}

// Same start values for every approach.
glm::vec2 startPosition(int i)
{
    return {static_cast<float>(i % 1000), std::floor(static_cast<float>(i) / 1000.0F)};
}
glm::vec2 startVelocity(int i)
{
    return {static_cast<float>((i % 7) - 3), static_cast<float>((i % 5) - 2)};
}

// Runs warmup + iterations updates and returns the measured durations in milliseconds.
template <typename Update>
std::vector<double> measure(const Options& options, Update update)
{
    for (int i = 0; i < options.warmup; ++i) {
        update();
    }
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(options.iterations));
    for (int i = 0; i < options.iterations; ++i) {
        const auto start = std::chrono::steady_clock::now();
        update();
        const auto stop = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
    }
    return times;
}

double runEcs(const Options& options, std::vector<double>& times)
{
    astra2d::Registry registry;
    for (int i = 0; i < options.entities; ++i) {
        const astra2d::Entity entity = registry.create();
        registry.emplace<Position>(entity, startPosition(i));
        registry.emplace<Velocity>(entity, startVelocity(i));
        registry.emplace<Appearance>(entity);
        registry.emplace<Health>(entity);
    }
    auto view = registry.view<Position, Velocity>();
    times = measure(options, [&] {
        view.each([](Position& p, const Velocity& v) { p.value += v.value * kDeltaTime; });
    });
    double checksum = 0.0;
    view.each([&](const Position& p, const Velocity&) { checksum += p.value.x + p.value.y; });
    return checksum;
}

double runOop(const Options& options, bool shuffled, std::vector<double>& times)
{
    std::vector<std::unique_ptr<GameObject>> objects;
    objects.reserve(static_cast<std::size_t>(options.entities));
    for (int i = 0; i < options.entities; ++i) {
        objects.push_back(std::make_unique<Mover>(startPosition(i), startVelocity(i)));
    }
    if (shuffled) {
        // A fixed seed on purpose: every run and approach shuffles the same way.
        std::mt19937 random(42); // NOLINT(bugprone-random-generator-seed)
        std::ranges::shuffle(objects, random);
    }
    times = measure(options, [&] {
        for (const auto& object : objects) {
            object->update(kDeltaTime);
        }
    });
    double checksum = 0.0;
    for (const auto& object : objects) {
        checksum += object->position().x + object->position().y;
    }
    return checksum;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Options options = parseOptions(std::span(argv, static_cast<std::size_t>(argc)));
        if (std::cmp_greater(options.entities, astra2d::Entity::kMaxEntities)) {
            throw std::invalid_argument("--entities is above Entity::kMaxEntities");
        }

        std::vector<double> times;
        const double checksum = options.approach == "ecs"
                                    ? runEcs(options, times)
                                    : runOop(options, options.approach == "oop_shuffled", times);

        const auto count = static_cast<double>(times.size());
        const double mean = std::accumulate(times.begin(), times.end(), 0.0) / count;
        double variance = 0.0;
        for (const double t : times) {
            variance += (t - mean) * (t - mean);
        }
        const double stddev = std::sqrt(variance / count);
        const auto [minIt, maxIt] = std::ranges::minmax_element(times);

        if (options.header) {
            std::cout << "approach,entities,iterations,mean_ms,stddev_ms,min_ms,max_ms,"
                         "ns_per_entity\n";
        }
        std::cout << std::format("{},{},{},{:.4f},{:.4f},{:.4f},{:.4f},{:.3f}\n", options.approach,
                                 options.entities, options.iterations, mean, stddev, *minIt, *maxIt,
                                 mean * 1e6 / options.entities);
        std::cerr << std::format("checksum {:.3f}\n", checksum);
    } catch (const std::exception& error) {
        std::cerr << "ecs_benchmark: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
