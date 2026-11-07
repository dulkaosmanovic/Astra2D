// Astra Run: the showcase platformer of Astra2D. Guide a stranded astronaut through three levels:
// run, jump, collect coins, stomp crawlers, avoid spikes and lava, and reach the flag.
//
// Controls (keyboard / gamepad):
//   A D, arrows / left stick, d-pad    run
//   Space, W, Up, Z / A                 jump (hold for higher jumps)
//   S, Down + jump / d-pad down + A     drop through a plank
//   Enter / A                           start, continue
//   P / Start                           pause
//   R / Back                            restart the level
//   M                                   mute
//   F3                                  physics debug view (shapes, contacts, velocities)
//   F2                                  log FPS, frame times and render statistics
//   Escape                              quit
//
// `platformer 2` starts at level 2 (handy while editing a level).
//
// The code is split by what it does:
//   level.hpp/.cpp        levels as data: text maps (level1.txt) and Tiled maps (level2/3.tmj)
//   controller.hpp/.cpp   movement rules: acceleration, jump, coyote time, jump buffering
//   game.cpp              the game: entities, systems, physics events, camera, HUD, sound
// level and controller do not use OpenGL and are unit-tested in tests/test_platformer.cpp.
// The assets are made by scripts/generate_platformer_assets.py (original, CC0).

#include "game.hpp"

#include <astra2d/core/log.hpp>

#include <charconv>
#include <cstring>
#include <exception>
#include <span>
#include <stdexcept>
#include <string>

namespace {

// The level number from the command line (1 if none is given).
int firstLevel(std::span<char*> args)
{
    if (args.size() < 2) {
        return 1;
    }
    const char* text = args[1];
    const char* end = text + std::strlen(text);
    int level = 0;
    const auto [stop, error] = std::from_chars(text, end, level);
    // Below 1 is rejected here: the caller subtracts 1, which would overflow for INT_MIN.
    if (error != std::errc{} || stop != end || level < 1) {
        throw std::invalid_argument(std::string("Usage: platformer [level number], not '") + text +
                                    "'");
    }
    return level;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        platformer::runGame(firstLevel(std::span(argv, static_cast<std::size_t>(argc))) - 1);
    } catch (const std::exception& error) {
        ASTRA_CRITICAL("{}", error.what());
        return 1;
    }
    return 0;
}
