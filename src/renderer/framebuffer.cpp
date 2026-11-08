#include "astra2d/renderer/framebuffer.hpp"

#include "astra2d/renderer/texture.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <format>
#include <stdexcept>
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

} // namespace astra2d
