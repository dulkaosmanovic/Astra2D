#include "astra2d/renderer/texture.hpp"

#include "astra2d/core/assert.hpp"
#include "astra2d/core/log.hpp"

#include <glad/gl.h>
#include <stb_image.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace astra2d {

namespace {

GLint glFilter(TextureFilter filter)
{
    return filter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR;
}

GLint glWrap(TextureWrap wrap)
{
    return wrap == TextureWrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
}

struct StbiDeleter {
    void operator()(stbi_uc* pixels) const noexcept { stbi_image_free(pixels); }
};

} // namespace

Texture2D::Texture2D(int width, int height, const std::uint8_t* bottomUpPixels,
                     const TextureSettings& settings)
    : m_width(width)
    , m_height(height)
{
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument(std::format("Invalid texture size {}x{}", width, height));
    }
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter(settings.filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter(settings.filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glWrap(settings.wrap));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glWrap(settings.wrap));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // rows are tightly packed
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 bottomUpPixels);
}

Texture2D Texture2D::empty(int width, int height, const TextureSettings& settings)
{
    return {width, height, nullptr, settings};
}

Texture2D::Texture2D(int width, int height, std::span<const std::uint8_t> pixels,
                     const TextureSettings& settings)
    : Texture2D(width, height, flipRows(width, height, pixels).data(), settings)
{
}

std::vector<std::uint8_t> Texture2D::flipRows(int width, int height,
                                              std::span<const std::uint8_t> pixels)
{
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument(std::format("Invalid texture size {}x{}", width, height));
    }
    const auto expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    if (pixels.size() != expected) {
        throw std::invalid_argument(std::format("Texture {}x{} needs {} bytes of RGBA, got {}",
                                                width, height, expected, pixels.size()));
    }

    // OpenGL expects the bottom row first. Uploading the rows in reverse puts the image's top row
    // at v = 1, so textures appear upright in a y-up world.
    const auto rowBytes = static_cast<std::size_t>(width) * 4U;
    const auto rows = static_cast<std::size_t>(height);
    std::vector<std::uint8_t> flipped(pixels.size());
    for (std::size_t row = 0; row < rows; ++row) {
        const auto source = pixels.subspan(row * rowBytes, rowBytes);
        const auto destination = (rows - 1U - row) * rowBytes;
        std::ranges::copy(source, flipped.begin() + static_cast<std::ptrdiff_t>(destination));
    }
    return flipped;
}

Texture2D Texture2D::fromFile(const std::filesystem::path& path, const TextureSettings& settings)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::unique_ptr<stbi_uc, StbiDeleter> pixels(
        stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha));
    if (!pixels) {
        throw std::runtime_error(
            std::format("Cannot load texture '{}': {}", path.string(), stbi_failure_reason()));
    }

    const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    Texture2D texture(width, height, std::span<const std::uint8_t>(pixels.get(), size), settings);
    ASTRA_CORE_DEBUG("Loaded texture '{}' ({}x{})", path.string(), width, height);
    return texture;
}

Texture2D::~Texture2D()
{
    glDeleteTextures(1, &m_texture); // 0 is ignored
}

Texture2D::Texture2D(Texture2D&& other) noexcept
    : m_texture(std::exchange(other.m_texture, 0))
    , m_width(std::exchange(other.m_width, 0))
    , m_height(std::exchange(other.m_height, 0))
{
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept
{
    if (this != &other) {
        glDeleteTextures(1, &m_texture);
        m_texture = std::exchange(other.m_texture, 0);
        m_width = std::exchange(other.m_width, 0);
        m_height = std::exchange(other.m_height, 0);
    }
    return *this;
}

void Texture2D::bind(int slot) const
{
    ASTRA_ASSERT(slot >= 0, "Texture slot must be >= 0, got {}", slot);
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(slot));
    glBindTexture(GL_TEXTURE_2D, m_texture);
}

} // namespace astra2d
