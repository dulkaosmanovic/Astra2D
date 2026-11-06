#pragma once

#include "astra2d/renderer/shader.hpp"
#include "astra2d/renderer/vertex_array.hpp"

#include <filesystem>
#include <string_view>

/// @addtogroup renderer
/// @{

namespace astra2d {

class Texture2D;

/// Full-screen effects: draws a texture (usually a Framebuffer's colour texture) to the current
/// render target through a fragment shader.
///
/// An effect is a fragment shader that receives:
///
/// | Input | Meaning |
/// |---|---|
/// | `in vec2 vUv` | texture coordinate, (0, 0) bottom-left to (1, 1) top-right |
/// | `uniform sampler2D uTexture` | the input image |
/// | `uniform vec2 uResolution` | size of the output in pixels |
/// | `uniform float uTime` | the time passed to apply(), in seconds |
///
/// @code
/// #version 330 core
/// in vec2 vUv;
/// uniform sampler2D uTexture;
/// out vec4 fragColor;
/// void main() { fragColor = vec4(vec3(1.0) - texture(uTexture, vUv).rgb, 1.0); } // invert
/// @endcode
///
/// The vertex stage is the same for every effect (a single triangle covering the screen); build
/// effects with createEffect() / loadEffect(), or AssetCache::effect() to get hot reloading.
class PostProcess {
public:
    /// Creates the full-screen geometry and the built-in copy effect. Needs an OpenGL context.
    PostProcess();

    /// The vertex shader every effect is linked with.
    [[nodiscard]] static std::string_view vertexShader();
    /// Compiles an effect from fragment shader source.
    /// @throws std::runtime_error if it does not compile or link.
    [[nodiscard]] static Shader createEffect(std::string_view fragmentSource);
    /// Loads and compiles an effect from a fragment shader file.
    /// @throws std::runtime_error if the file cannot be read or does not compile or link.
    [[nodiscard]] static Shader loadEffect(const std::filesystem::path& fragmentPath);

    /// Draws @p input to the current render target, covering its whole viewport, through
    /// @p effect. Blending is turned off for the draw.
    void apply(const Texture2D& input, Shader& effect, float time = 0.0F);
    /// Draws @p input unchanged (the built-in copy effect).
    void copy(const Texture2D& input);

private:
    VertexArray m_emptyVertexArray; // core profile needs a VAO even without vertex attributes
    Shader m_copy;
};

} // namespace astra2d

/// @}
