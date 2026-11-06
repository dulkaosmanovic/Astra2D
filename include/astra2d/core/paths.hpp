#pragma once

#include <filesystem>
#include <initializer_list>

/// @addtogroup core
/// @{

namespace astra2d {

/// The directory that contains the running executable, e.g. `/opt/mygame/bin`.
///
/// A shipped game cannot rely on the working directory (it depends on how the game was started)
/// or on paths compiled in at build time (the source tree is not on the player's machine). Its
/// files are found relative to the executable instead:
/// @code
/// AssetCache assets(executableDirectory() / "assets");
/// @endcode
///
/// @throws std::runtime_error if the path cannot be determined (Linux: `/proc/self/exe`).
[[nodiscard]] std::filesystem::path executableDirectory();

/// The first of @p candidates that is an existing directory. Relative candidates are taken as
/// they are, i.e. relative to the working directory. Typical use: the assets next to the
/// installed executable first, then the source tree's during development:
/// @code
/// const auto root = findDirectory({executableDirectory() / "assets", GAME_SOURCE_ASSETS_DIR});
/// @endcode
/// @throws std::runtime_error listing every candidate if none exists.
[[nodiscard]] std::filesystem::path
findDirectory(std::initializer_list<std::filesystem::path> candidates);

} // namespace astra2d

/// @}
