#include "astra2d/core/paths.hpp"

#include <format>
#include <stdexcept>
#include <string>
#include <system_error>

namespace astra2d {

std::filesystem::path executableDirectory()
{
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        throw std::runtime_error(
            std::format("Cannot determine the executable's path: {}", error.message()));
    }
    return executable.parent_path();
}

std::filesystem::path findDirectory(std::initializer_list<std::filesystem::path> candidates)
{
    std::string tried;
    for (const auto& candidate : candidates) {
        std::error_code error; // an unreadable candidate is skipped, not an exception
        if (std::filesystem::is_directory(candidate, error)) {
            return candidate;
        }
        tried += std::format("\n  {}", candidate.string());
    }
    throw std::runtime_error(std::format("None of these directories exists:{}", tried));
}

} // namespace astra2d
