#pragma once

namespace platformer {

/// Opens the window and plays the game until it is closed. @p firstLevel (0-based) is where
/// "start" begins, e.g. to try out a level while editing it.
/// @throws std::exception if an asset or level cannot be loaded, or firstLevel does not exist.
void runGame(int firstLevel = 0);

} // namespace platformer
