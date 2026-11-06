#include "astra2d/assets/file_watcher.hpp"

#include <system_error>

namespace astra2d {

std::optional<std::filesystem::file_time_type>
FileWatcher::modificationTime(const std::filesystem::path& file)
{
    std::error_code error;
    const auto time = std::filesystem::last_write_time(file, error);
    if (error) {
        return std::nullopt;
    }
    return time;
}

void FileWatcher::watch(const std::filesystem::path& file)
{
    m_files.try_emplace(file.string(), Entry{.path = file, .modified = modificationTime(file)});
}

void FileWatcher::unwatch(const std::filesystem::path& file)
{
    m_files.erase(file.string());
}

bool FileWatcher::isWatching(const std::filesystem::path& file) const
{
    return m_files.contains(file.string());
}

std::vector<std::filesystem::path> FileWatcher::poll()
{
    std::vector<std::filesystem::path> changed;
    for (auto& [key, entry] : m_files) {
        const auto modified = modificationTime(entry.path);
        if (!modified) {
            continue; // missing right now (e.g. in the middle of an editor's save): check later
        }
        if (modified != entry.modified) {
            entry.modified = modified;
            changed.push_back(entry.path);
        }
    }
    return changed;
}

} // namespace astra2d
