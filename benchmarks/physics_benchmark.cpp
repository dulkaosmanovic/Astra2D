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
//   physics_benchmark stack --boxes 10 [--offset 0] [--seed 1] [--iterations 8] [--seconds 10]
//                           [--header]
//     A tower of unit boxes on the ground, without sleeping. With --offset, every box is moved
//     sideways by a random amount up to that many units (a tower stacked by hand), else they are
//     perfectly aligned. --iterations sets the solver's velocity iterations. Prints
//       boxes,offset,seed,iterations,seconds,max_drift_x,top_error_y,max_angle,standing
//     max_drift_x: largest sideways movement of any box from where it was placed; top_error_y:
//     how far the top box is below where it was placed (overlap of all contacts); max_angle:
//     largest rotation in radians; standing: 1 if the top box is still within half a box of its
//     height at the end, 0 if the tower fell.
//   physics_benchmark bounce --restitution 1 [--seconds 10] [--header]
//     A ball dropped from 5 units onto the ground. Prints
//       restitution,seconds,drop_height,first_apex,last_apex,max_apex,bounces
//     Heights of the ball's lowest point at the top of each bounce. With restitution 1 every apex
//     should reach the drop height, with 0.5 each should be a quarter of the one before (the
//     speed halves), with 0 the ball should not bounce at all (no apex: 0).
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
    float offset = 0.0F;
    unsigned seed = 1;
    int iterations = 8;
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
        } else if (arg == "--offset") {
            options.offset = parseNumber<float>(next(), arg);
        } else if (arg == "--seed") {
            options.seed = parseNumber<unsigned>(next(), arg);
        } else if (arg == "--iterations") {
            options.iterations = std::max(1, parseNumber<int>(next(), arg));
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
    // About 4 square units per body: loose enough to move. Only a few bodies touch at a time (about
    // 80 of 5,000), so the step time is mostly the broad phase, which is what E3 compares.
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
    PhysicsWorld world({.velocityIterations = options.iterations, .enableSleep = false});
    addGround(world);
    std::mt19937 random(options.seed);
    std::uniform_real_distribution<float> offset(-options.offset, options.offset);
    std::vector<BodyId> boxes;
    std::vector<float> startX;
    for (int i = 0; i < options.boxes; ++i) {
        const float x = options.offset > 0.0F ? offset(random) : 0.0F;
        const BodyId box = world.createBody({.position = {x, 0.5F + static_cast<float>(i)}});
        world.addShape(box, ShapeDef::makeBox(glm::vec2(0.5F)));
        boxes.push_back(box);
        startX.push_back(x);
    }
    float maxDrift = 0.0F;
    float maxAngle = 0.0F;
    for (int i = 0; i < steps(options.seconds); ++i) {
        world.step(kDt);
        for (std::size_t b = 0; b < boxes.size(); ++b) {
            maxDrift = std::max(maxDrift, std::abs(world.position(boxes[b]).x - startX[b]));
            maxAngle = std::max(maxAngle, std::abs(world.angle(boxes[b])));
        }
    }
    const float topError =
        (0.5F + static_cast<float>(options.boxes - 1)) - world.position(boxes.back()).y;
    if (options.header) {
        std::cout << "boxes,offset,seed,iterations,seconds,max_drift_x,top_error_y,max_angle,"
                     "standing\n";
    }
    std::cout << std::format("{},{:.3f},{},{},{:.1f},{:.6f},{:.6f},{:.6f},{}\n", options.boxes,
                             options.offset, options.seed, options.iterations, options.seconds,
                             maxDrift, topError, maxAngle, topError < 0.5F ? 1 : 0);
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

    // The height of the ball's lowest point. Measured at the top of each bounce (where the
    // vertical velocity turns from up to down), not as kinetic + potential energy every step:
    // the semi-implicit Euler integrator makes that sum swing by up to g·|v|·dt/2 within a
    // step, which would show up as energy gained or lost that the ball never has.
    const auto height = [&] { return world.position(ball).y - radius; };
    const float drop = height();
    float firstApex = 0.0F;
    float lastApex = 0.0F;
    float maxApex = 0.0F;
    int bounces = 0;
    float previousVy = 0.0F;
    for (int i = 0; i < steps(options.seconds); ++i) {
        world.step(kDt);
        bounces += static_cast<int>(world.contactBeginEvents().size());
        const float vy = world.velocity(ball).y;
        if (previousVy > 0.0F && vy <= 0.0F) {
            lastApex = height();
            firstApex = firstApex > 0.0F ? firstApex : lastApex;
            maxApex = std::max(maxApex, lastApex);
        }
        previousVy = vy;
    }
    if (options.header) {
        std::cout << "restitution,seconds,drop_height,first_apex,last_apex,max_apex,bounces\n";
    }
    std::cout << std::format("{:.2f},{:.1f},{:.4f},{:.4f},{:.4f},{:.4f},{}\n", options.restitution,
                             options.seconds, drop, firstApex, lastApex, maxApex, bounces);
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
