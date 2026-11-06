#include "astra2d/renderer/framebuffer.hpp"
#include "astra2d/renderer/post_process.hpp"

#include "astra2d/renderer/texture.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace astra2d {

// ---------------------------------------------------------------------------------------------
// Framebuffer

Framebuffer::Framebuffer(int width, int height, const TextureSettings& colorSettings)
    : m_color(Texture2D::empty(width, height, colorSettings))
    , m_colorSettings(colorSettings)
{
    glGenFramebuffers(1, &m_framebuffer);
    attach();
}

Framebuffer::~Framebuffer()
{
    glDeleteFramebuffers(1, &m_framebuffer); // 0 is ignored
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : m_color(std::move(other.m_color))
    , m_colorSettings(other.m_colorSettings)
    , m_framebuffer(std::exchange(other.m_framebuffer, 0))
{
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept
{
    if (this != &other) {
        glDeleteFramebuffers(1, &m_framebuffer);
        m_color = std::move(other.m_color);
        m_colorSettings = other.m_colorSettings;
        m_framebuffer = std::exchange(other.m_framebuffer, 0);
    }
    return *this;
}

void Framebuffer::attach()
{
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color.id(), 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(std::format("Framebuffer {}x{} is incomplete (status {:#x})",
                                             m_color.width(), m_color.height(), status));
    }
}

void Framebuffer::bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glViewport(0, 0, m_color.width(), m_color.height());
}

void Framebuffer::bindDefault(int width, int height)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
}

void Framebuffer::resize(int width, int height)
{
    if (width == m_color.width() && height == m_color.height()) {
        return;
    }
    m_color = Texture2D::empty(width, height, m_colorSettings);
    attach();
}

void Framebuffer::blitToWindow(int windowWidth, int windowHeight, bool keepAspect,
                               TextureFilter filter) const
{
    int x = 0;
    int y = 0;
    int width = windowWidth;
    int height = windowHeight;
    if (keepAspect) {
        const float scale =
            std::min(static_cast<float>(windowWidth) / static_cast<float>(m_color.width()),
                     static_cast<float>(windowHeight) / static_cast<float>(m_color.height()));
        width = static_cast<int>(static_cast<float>(m_color.width()) * scale);
        height = static_cast<int>(static_cast<float>(m_color.height()) * scale);
        x = (windowWidth - width) / 2;
        y = (windowHeight - height) / 2;
    }

    bindDefault(windowWidth, windowHeight);
    if (keepAspect) {
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT); // the bars around the image
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);
    glBlitFramebuffer(0, 0, m_color.width(), m_color.height(), x, y, x + width, y + height,
                      GL_COLOR_BUFFER_BIT,
                      filter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------------------------
// PostProcess

namespace {

// One triangle that covers the whole screen: corners (-1,-1), (3,-1), (-1,3) in clip space,
// generated from gl_VertexID so no vertex buffer is needed. The part outside the screen is clipped.
constexpr const char* kFullscreenVertexShader = R"(#version 330 core
out vec2 vUv;

void main()
{
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* kCopyFragmentShader = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTexture;
out vec4 fragColor;

void main()
{
    fragColor = texture(uTexture, vUv);
}
)";

std::string readText(const std::filesystem::path& path)
{
    const std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::format("Cannot open effect file '{}'", path.string()));
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

} // namespace

PostProcess::PostProcess()
    : m_copy(createEffect(kCopyFragmentShader))
{
}

std::string_view PostProcess::vertexShader()
{
    return kFullscreenVertexShader;
}

Shader PostProcess::createEffect(std::string_view fragmentSource)
{
    return {kFullscreenVertexShader, fragmentSource};
}

Shader PostProcess::loadEffect(const std::filesystem::path& fragmentPath)
{
    try {
        return createEffect(readText(fragmentPath));
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(
            std::format("{} (effect '{}')", error.what(), fragmentPath.string()));
    }
}

void PostProcess::apply(const Texture2D& input, Shader& effect, float time)
{
    std::array<GLint, 4> viewport{};
    glGetIntegerv(GL_VIEWPORT, viewport.data());

    glDisable(GL_BLEND);
    effect.setInt("uTexture", 0);
    effect.setVec2("uResolution",
                   {static_cast<float>(viewport[2]), static_cast<float>(viewport[3])});
    effect.setFloat("uTime", time);
    input.bind(0);
    m_emptyVertexArray.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void PostProcess::copy(const Texture2D& input)
{
    apply(input, m_copy);
}

} // namespace astra2d
