#pragma once

// Shared by the examples: where to find assets/.

#include <cstdlib>
#include <filesystem>

namespace examples {

/// The assets directory: the ASTRA2D_ASSETS_DIR environment variable if set (so a moved or
/// installed binary still finds its files), otherwise the source tree's assets/ directory that
/// examples/meson.build compiles in.
inline std::filesystem::path assetsDirectory()
{
    // Read once at start-up, before any other thread exists.
    const char* overridden = std::getenv("ASTRA2D_ASSETS_DIR"); // NOLINT(concurrency-mt-unsafe)
    if (overridden != nullptr && *overridden != '\0') {
        return overridden;
    }
    return ASTRA2D_ASSETS_DIR;
}

/// Frame layout of assets/textures/sprites.png (see scripts/generate_assets.py): 16x16 frames,
/// each with a 1-texel extruded border.
inline constexpr int kSpriteFrame = 16;
inline constexpr int kSpriteMargin = 1;
inline constexpr int kSpriteSpacing = 2;

} // namespace examples
