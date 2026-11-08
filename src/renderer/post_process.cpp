#include "astra2d/renderer/post_process.hpp"

#include "astra2d/renderer/texture.hpp"
#include "text_file.hpp"

#include <glad/gl.h>

#include <array>
#include <format>
#include <stdexcept>

namespace astra2d {

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

} // namespace

PostProcess::PostProcess()
    : m_copy(createEffect(kCopyFragmentShader))
{
}

Shader PostProcess::createEffect(std::string_view fragmentSource)
{
    return {kFullscreenVertexShader, fragmentSource};
}

Shader PostProcess::loadEffect(const std::filesystem::path& fragmentPath)
{
    try {
        return createEffect(detail::readTextFile(fragmentPath));
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
