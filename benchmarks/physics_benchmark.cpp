// Evaluations E3 and E4 of the physics, headless.
//
// E3 "How much does the broad phase reduce collision cost?"
//   physics_benchmark broadphase --method brute|sap|grid --bodies 1000 [--steps 300] [--warmup 60]
//                                [--header]
//   N bodies (circles and boxes, 0.5 to 1 units) move without gravity in a closed box with
//   elastic walls, like a gas: they stay spread out, so every step costs about the same. Prints
//     method,bodies,steps,step_ms,stddev_ms,broadphase_ms,narrowphase_ms,solver_ms,pair_tests,
//     candidate_pairs,touching
//   (all means per step). Every method finds the same pairs, so only the cost differs.
//
// E4 "Is the physics stable?"
//   physics_benchmark stack --boxes 10 [--seconds 10] [--header]
//     A tower of unit boxes on the ground, without sleeping. Prints
//       boxes,seconds,max_drift_x,top_error_y,max_angle
//     max_drift_x: largest sideways offset of any box; top_error_y: how far the top box is below
//     where it was placed (overlap of all contacts); max_angle: largest rotation in radians.
//   physics_benchmark bounce --restitution 1 [--seconds 10] [--header]
//     A ball dropped from 5 units onto the ground. Prints
//       restitution,seconds,energy_start,energy_end,energy_max,bounces
//     Energy is kinetic + potential per unit mass; with restitution 1 it should stay, with 0 the
//     ball should come to rest (energy_end = energy at rest).
//
// A checksum of the final state goes to stderr, so the compiler cannot skip the work.

#include <astra2d/physics/world.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <numeric>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using astra2d::BodyId;
using astra2d::BodyType;
using astra2d::PhysicsWorld;
using astra2d::ShapeDef;

constexpr float kDt = 1.0F / 60.0F;

struct Options {
    std::string experiment;
    std::string method = "grid";
    int bodies = 1000;
    int steps = 300;
    int warmup = 60;
    int boxes = 10;
    float seconds = 10.0F;
    float restitution = 1.0F;
    bool header = false;
};

template <typename T>
T parseNumber(std::string_view text, const std::string& name)
{
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < T{}) {
        throw std::invalid_argument(name + " needs a non-negative number");
    }
    return value;
}

Options parseOptions(std::span<char*> args)
{
    if (args.size() < 2) {
        throw std::invalid_argument("usage: physics_benchmark broadphase|stack|bounce [options]");
    }
    Options options;
    options.experiment = args[1];
    for (std::size_t i = 2; i < args.size(); ++i) {
        const std::string arg = args[i];
        const auto next = [&]() -> std::string_view {
            if (i + 1 >= args.size()) {
                throw std::invalid_argument(arg + " needs a value");
            }
            return args[++i];
        };
        if (arg == "--method") {
            options.method = next();
        } else if (arg == "--bodies") {
            options.bodies = parseNumber<int>(next(), arg);
        } else if (arg == "--steps") {
            options.steps = std::max(1, parseNumber<int>(next(), arg));
        } else if (arg == "--warmup") {
            options.warmup = parseNumber<int>(next(), arg);
        } else if (arg == "--boxes") {
            options.boxes = parseNumber<int>(next(), arg);
        } else if (arg == "--seconds") {
            options.seconds = parseNumber<float>(next(), arg);
        } else if (arg == "--restitution") {
            options.restitution = parseNumber<float>(next(), arg);
        } else if (arg == "--header") {
            options.header = true;
        } else {
            throw std::invalid_argument("unknown argument " + arg);
        }
    }
    // 0 bodies makes an empty arena (invalid random range), 0 boxes an empty stack to measure.
    if (options.bodies < 1 || options.boxes < 1) {
        throw std::invalid_argument("--bodies and --boxes need at least 1");
    }
    return options;
}

astra2d::BroadPhaseType methodType(const std::string& method)
{
    if (method == "brute") {
        return astra2d::BroadPhaseType::BruteForce;
    }
    if (method == "sap") {
        return astra2d::BroadPhaseType::SortAndSweep;
    }
    if (method == "grid") {
        return astra2d::BroadPhaseType::UniformGrid;
    }
    throw std::invalid_argument("--method must be brute, sap or grid");
}

int steps(float seconds)
{
    return static_cast<int>(std::lround(seconds / kDt));
}

// ---------------------------------------------------------------------------------------------
// E3

void broadPhase(const Options& options)
{
    PhysicsWorld world(
        {.gravity = {0.0F, 0.0F}, .enableSleep = false, .broadPhase = methodType(options.method)});
    // About 4 square units per body: dense enough for many contacts, loose enough to move.
    const float half = std::sqrt(static_cast<float>(options.bodies) * 4.0F) / 2.0F;
    const BodyId walls = world.createBody({.type = BodyType::Static});
    for (const auto& [center, extents] :
         {std::pair{glm::vec2(0.0F, -half - 0.5F), glm::vec2(half + 1.0F, 0.5F)},
          std::pair{glm::vec2(0.0F, half + 0.5F), glm::vec2(half + 1.0F, 0.5F)},
          std::pair{glm::vec2(-half - 0.5F, 0.0F), glm::vec2(0.5F, half + 1.0F)},
          std::pair{glm::vec2(half + 0.5F, 0.0F), glm::vec2(0.5F, half + 1.0F)}}) {
        ShapeDef wall = ShapeDef::makeBox(extents, center);
        wall.restitution = 1.0F;
        wall.friction = 0.0F;
        world.addShape(walls, wall);
    }
    std::mt19937 random(7); // NOLINT(bugprone-random-generator-seed): same scene for every method
    std::uniform_real_distribution<float> position(-half + 1.0F, half - 1.0F);
    std::uniform_real_distribution<float> speed(-3.0F, 3.0F);
    std::uniform_real_distribution<float> size(0.25F, 0.5F);
    for (int i = 0; i < options.bodies; ++i) {
        const BodyId body = world.createBody({.position = {position(random), position(random)},
                                              .velocity = {speed(random), speed(random)}});
        ShapeDef shape = i % 2 == 0 ? ShapeDef::makeCircle(size(random))
                                    : ShapeDef::makeBox({size(random), size(random)});
        shape.restitution = 0.9F;
        shape.friction = 0.1F;
        world.addShape(body, shape);
    }

    for (int i = 0; i < options.warmup; ++i) {
        world.step(kDt);
    }
    std::vector<double> times;
    double broad = 0.0;
    double narrow = 0.0;
    double solver = 0.0;
    double tests = 0.0;
    double candidates = 0.0;
    double touching = 0.0;
    for (int i = 0; i < options.steps; ++i) {
        const auto start = std::chrono::steady_clock::now();
        world.step(kDt);
        times.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
        const astra2d::PhysicsStats& stats = world.stats();
        broad += stats.broadPhaseMs;
        narrow += stats.narrowPhaseMs;
        solver += stats.solverMs;
        tests += static_cast<double>(stats.pairTests);
        candidates += stats.candidatePairs;
        touching += stats.touchingContacts;
    }
    const auto n = static_cast<double>(times.size());
    const double mean = std::accumulate(times.begin(), times.end(), 0.0) / n;
    double variance = 0.0;
    for (const double t : times) {
        variance += (t - mean) * (t - mean);
    }
    if (options.header) {
        std::cout << "method,bodies,steps,step_ms,stddev_ms,broadphase_ms,narrowphase_ms,solver_ms,"
                     "pair_tests,"
                     "candidate_pairs,touching\n";
    }
    std::cout << std::format("{},{},{},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.0f},{:.1f},{:.1f}\n",
                             options.method, options.bodies, options.steps, mean,
                             std::sqrt(variance / n), broad / n, narrow / n, solver / n, tests / n,
                             candidates / n, touching / n);
    double checksum = 0.0;
    world.forEachBody(
        [&](BodyId body) { checksum += world.position(body).x + world.position(body).y; });
    std::cerr << std::format("checksum {:.4f}\n", checksum);
}

// ---------------------------------------------------------------------------------------------
// E4

BodyId addGround(PhysicsWorld& world)
{
    const BodyId ground = world.createBody({.type = BodyType::Static, .position = {0.0F, -0.5F}});
    world.addShape(ground, ShapeDef::makeBox({50.0F, 0.5F}));
    return ground;
}

void stack(const Options& options)
{
    PhysicsWorld world({.enableSleep = false});
    addGround(world);
    std::vector<BodyId> boxes;
    for (int i = 0; i < options.boxes; ++i) {
        const BodyId box = world.createBody({.position = {0.0F, 0.5F + static_cast<float>(i)}});
        world.addShape(box, ShapeDef::makeBox(glm::vec2(0.5F)));
        boxes.push_back(box);
    }
    float maxDrift = 0.0F;
    float maxAngle = 0.0F;
    for (int i = 0; i < steps(options.seconds); ++i) {
        world.step(kDt);
        for (const BodyId box : boxes) {
            maxDrift = std::max(maxDrift, std::abs(world.position(box).x));
            maxAngle = std::max(maxAngle, std::abs(world.angle(box)));
        }
    }
    const float topError =
        (0.5F + static_cast<float>(options.boxes - 1)) - world.position(boxes.back()).y;
    if (options.header) {
        std::cout << "boxes,seconds,max_drift_x,top_error_y,max_angle\n";
    }
    std::cout << std::format("{},{:.1f},{:.6f},{:.6f},{:.6f}\n", options.boxes, options.seconds,
                             maxDrift, topError, maxAngle);
    std::cerr << std::format("checksum {:.6f}\n", world.position(boxes.back()).y);
}

void bounce(const Options& options)
{
    PhysicsWorld world({.enableSleep = false});
    const BodyId ground = addGround(world);
    world.setMaterial(world.shapes(ground)[0], 0.5F, options.restitution);
    const float radius = 0.5F;
    const BodyId ball = world.createBody({.position = {0.0F, 5.0F}});
    ShapeDef shape = ShapeDef::makeCircle(radius);
    shape.restitution = options.restitution;
    world.addShape(ball, shape);

    const float g = -world.settings().gravity.y;
    const auto energy = [&] {
        const glm::vec2 v = world.velocity(ball);
        // Potential energy measured from the resting height (the ball's centre at its radius).
        return (0.5F * glm::dot(v, v)) + (g * (world.position(ball).y - radius));
    };
    const float start = energy();
    float highest = start;
    int bounces = 0;
    for (int i = 0; i < steps(options.seconds); ++i) {
        world.step(kDt);
        highest = std::max(highest, energy());
        bounces += static_cast<int>(world.contactBeginEvents().size());
    }
    if (options.header) {
        std::cout << "restitution,seconds,energy_start,energy_end,energy_max,bounces\n";
    }
    std::cout << std::format("{:.2f},{:.1f},{:.4f},{:.4f},{:.4f},{}\n", options.restitution,
                             options.seconds, start, energy(), highest, bounces);
    std::cerr << std::format("checksum {:.6f}\n", world.position(ball).y);
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Options options = parseOptions(std::span(argv, static_cast<std::size_t>(argc)));
        if (options.experiment == "broadphase") {
            broadPhase(options);
        } else if (options.experiment == "stack") {
            stack(options);
        } else if (options.experiment == "bounce") {
            bounce(options);
        } else {
            throw std::invalid_argument("unknown experiment " + options.experiment);
        }
    } catch (const std::exception& error) {
        std::cerr << "physics_benchmark: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
