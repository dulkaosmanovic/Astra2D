#include "astra2d/assets/asset_cache.hpp"

#include "astra2d/core/log.hpp"
#include "astra2d/renderer/post_process.hpp"

#include <exception>
#include <format>
#include <utility>

namespace astra2d {

AssetCache::AssetCache(std::filesystem::path root)
    : m_root(std::move(root))
{
}

std::filesystem::path AssetCache::resolve(const std::filesystem::path& path) const
{
    const std::filesystem::path full = path.is_absolute() ? path : m_root / path;
    // weakly_canonical resolves "..", "." and symlinks as far as the path exists, and does not
    // require the file to exist (a missing file then fails in the loader with a clear message).
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(full, error);
    return error ? full.lexically_normal() : canonical;
}

void AssetCache::watch(const std::vector<std::filesystem::path>& files,
                       const std::string& description, const std::function<void()>& reload)
{
    for (const auto& file : files) {
        m_watcher.watch(file);
        m_reloaders[file.string()].push_back({.description = description, .reload = reload});
    }
}

template <typename T, typename Load>
T& AssetCache::loadWatched(ResourceCache<T>& cache, const std::string& key, const Load& load,
                           const std::vector<std::filesystem::path>& files,
                           const std::string& description)
{
    const bool loaded = cache.contains(key);
    T& asset = cache.get(key, load);
    if (!loaded) {
        watch(files, description, [&cache, key, load] { *cache.find(key) = load(); });
    }
    return asset;
}

Texture2D& AssetCache::texture(const std::filesystem::path& path, const TextureSettings& settings)
{
    const std::filesystem::path file = resolve(path);
    const std::string key =
        std::format("{}|{}|{}", file.string(), static_cast<int>(settings.filter),
                    static_cast<int>(settings.wrap));
    Texture2D& texture = loadWatched(m_textures, key, [file, settings] {
        return Texture2D::fromFile(file, settings);
    }, {file}, "texture " + file.string());
    m_textureSources.try_emplace(&texture, path, settings); // keeps the first spelling
    return texture;
}

Shader& AssetCache::shader(const std::filesystem::path& vertexPath,
                           const std::filesystem::path& fragmentPath)
{
    const std::filesystem::path vertex = resolve(vertexPath);
    const std::filesystem::path fragment = resolve(fragmentPath);
    const std::string key = vertex.string() + "|" + fragment.string();
    return loadWatched(m_shaders, key, [vertex, fragment] {
        return Shader::fromFiles(vertex, fragment);
    }, {vertex, fragment}, "shader " + key);
}

Shader& AssetCache::effect(const std::filesystem::path& fragmentPath)
{
    const std::filesystem::path fragment = resolve(fragmentPath);
    return loadWatched(m_shaders, "effect|" + fragment.string(), [fragment] {
        return PostProcess::loadEffect(fragment);
    }, {fragment}, "effect " + fragment.string());
}

std::size_t AssetCache::reloadChanged()
{
    std::size_t reloaded = 0;
    for (const auto& file : m_watcher.poll()) {
        const auto it = m_reloaders.find(file.string());
        if (it == m_reloaders.end()) {
            continue;
        }
        for (const Reloader& reloader : it->second) {
            try {
                // The loader builds the new object first and only then replaces the old one, so a
                // failure leaves the asset as it was.
                reloader.reload();
                ++reloaded;
                ASTRA_CORE_INFO("Reloaded {}", reloader.description);
            } catch (const std::exception& error) {
                ASTRA_CORE_ERROR("Reloading {} failed, keeping the old version: {}",
                                 reloader.description, error.what());
            }
        }
    }
    return reloaded;
}

void AssetCache::clear()
{
    m_textures.clear();
    m_textureSources.clear();
    m_shaders.clear();
    m_watcher.clear();
    m_reloaders.clear();
}

std::optional<AssetCache::TextureSource> AssetCache::textureSource(const Texture2D& texture) const
{
    const auto it = m_textureSources.find(&texture);
    if (it == m_textureSources.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace astra2d
