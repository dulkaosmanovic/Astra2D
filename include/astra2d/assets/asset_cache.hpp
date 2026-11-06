#pragma once

#include "astra2d/assets/file_watcher.hpp"
#include "astra2d/assets/resource_cache.hpp"
#include "astra2d/renderer/shader.hpp"
#include "astra2d/renderer/texture.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

/// @addtogroup assets
/// @{

namespace astra2d {

/// Loads textures and shaders from disk, each file only once.
///
/// Paths are relative to the root directory given to the constructor (absolute paths are used as
/// they are). Two requests for the same file return the same object, also when the path is
/// spelled differently (`a/../b.png` and `b.png`). A texture requested with different
/// TextureSettings is loaded again, since the sampling settings belong to the texture.
///
/// @code
/// AssetCache assets("assets");
/// const Texture2D& tiles = assets.texture("textures/tiles.png");
/// @endcode
///
/// Needs a current OpenGL context to load, like Texture2D and Shader. References stay valid until
/// clear() or the cache is destroyed.
///
/// **Hot reloading.** Every loaded file is watched. reloadChanged() reloads the assets whose files
/// changed on disk, *in place*: references handed out earlier now refer to the new version, so a
/// shader or texture edited while the game runs shows up without a restart. A reload that fails
/// (a shader with a syntax error, a half-written image) is logged and the old version is kept, so
/// the game keeps running while the file is fixed. Call it from Application::onUpdate(), e.g.
/// twice per second, never between Renderer2D::begin() and end():
/// @code
/// if ((m_reloadTimer += dt) > 0.5F) { m_reloadTimer = 0.0F; m_assets.reloadChanged(); }
/// @endcode
/// A reloaded texture is a new OpenGL texture with possibly another size; a SpriteSheet made from
/// it keeps its old frame layout.
class AssetCache {
public:
    /// @param root Directory that relative paths are resolved against; empty means the working
    ///             directory.
    explicit AssetCache(std::filesystem::path root = {});
    ~AssetCache() = default;
    // Not copyable or movable: the reloaders refer back to this object.
    AssetCache(const AssetCache&) = delete;
    AssetCache& operator=(const AssetCache&) = delete;
    AssetCache(AssetCache&&) = delete;
    AssetCache& operator=(AssetCache&&) = delete;

    /// The texture at @p path, loaded on first use.
    /// @throws std::runtime_error if the file cannot be loaded.
    Texture2D& texture(const std::filesystem::path& path, const TextureSettings& settings = {});

    /// The shader program built from the two files, loaded on first use.
    /// @throws std::runtime_error if a file cannot be read or the program does not compile.
    Shader& shader(const std::filesystem::path& vertexPath,
                   const std::filesystem::path& fragmentPath);

    /// A full-screen effect (see PostProcess) built from the fragment shader file @p fragmentPath
    /// and the built-in full-screen vertex shader, loaded on first use.
    /// @throws std::runtime_error if the file cannot be read or the effect does not compile.
    Shader& effect(const std::filesystem::path& fragmentPath);

    /// Reloads every asset whose file changed since it was loaded or last reloaded. Returns the
    /// number of assets reloaded successfully; failures are logged and keep the old version.
    std::size_t reloadChanged();

    /// Full path for @p path: relative paths are joined to the root and normalized.
    [[nodiscard]] std::filesystem::path resolve(const std::filesystem::path& path) const;

    /// The root directory.
    [[nodiscard]] const std::filesystem::path& root() const { return m_root; }

    /// Number of textures currently loaded.
    [[nodiscard]] std::size_t textureCount() const { return m_textures.size(); }
    /// Number of shaders currently loaded.
    [[nodiscard]] std::size_t shaderCount() const { return m_shaders.size(); }

    /// Unloads everything. All references handed out become dangling.
    void clear();

private:
    // Remembers how to reload an asset when one of its files changes.
    void watch(const std::vector<std::filesystem::path>& files, const std::string& description,
               const std::function<void()>& reload);

    struct Reloader {
        std::string description;
        std::function<void()> reload;
    };

    std::filesystem::path m_root;
    ResourceCache<Texture2D> m_textures;
    ResourceCache<Shader> m_shaders;
    FileWatcher m_watcher;
    std::unordered_map<std::string, std::vector<Reloader>> m_reloaders; // by file path
};

} // namespace astra2d

/// @}
