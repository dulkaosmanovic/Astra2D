#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/// @addtogroup assets
/// @{

namespace astra2d {

/// Detects changed files by polling their modification times.
///
/// Polling is portable and simple, and for the handful of files a game edits during development
/// (shaders, textures) checking them a few times per second costs next to nothing.
///
/// @code
/// watcher.watch("assets/shaders/crt.frag");
/// for (const auto& file : watcher.poll()) { reload(file); }
/// @endcode
///
/// A file that is missing when it is checked (editors often save by deleting and re-creating) is
/// not reported; it is reported once it exists again with a new modification time.
class FileWatcher {
public:
    /// Starts watching @p file. Its current modification time is the reference, so only later
    /// changes are reported. Watching a file twice has no effect.
    void watch(const std::filesystem::path& file);
    /// Stops watching @p file.
    void unwatch(const std::filesystem::path& file);
    /// Stops watching all files.
    void clear() { m_files.clear(); }

    /// True if @p file is watched.
    [[nodiscard]] bool isWatching(const std::filesystem::path& file) const;
    /// Number of watched files.
    [[nodiscard]] std::size_t size() const { return m_files.size(); }

    /// Files whose modification time changed since they were watched or last reported.
    [[nodiscard]] std::vector<std::filesystem::path> poll();

private:
    struct Entry {
        std::filesystem::path path;
        std::optional<std::filesystem::file_time_type> modified; // nullopt: did not exist
    };

    [[nodiscard]] static std::optional<std::filesystem::file_time_type>
    modificationTime(const std::filesystem::path& file);

    std::unordered_map<std::string, Entry> m_files;
};

} // namespace astra2d

/// @}
