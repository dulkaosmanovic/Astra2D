#pragma once

// Internal to the renderer: reads a shader or effect source file.

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace astra2d::detail {

/// The whole file as a string. @throws std::runtime_error if it cannot be opened.
inline std::string readTextFile(const std::filesystem::path& path)
{
    const std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::format("Cannot open file '{}'", path.string()));
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

} // namespace astra2d::detail
