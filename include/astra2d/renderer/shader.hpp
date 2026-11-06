#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

/// @addtogroup renderer
/// @{

namespace astra2d {

/// A linked GLSL program (vertex + fragment shader). Owns the OpenGL program object.
///
/// Move-only. Uniform locations are looked up once and cached. Setting a uniform that does not
/// exist (or was optimized out by the driver) is silently ignored, as in OpenGL itself.
///
/// @code
/// Shader shader = Shader::fromFiles("shaders/sprite.vert", "shaders/sprite.frag");
/// shader.bind();
/// shader.setMat4("uViewProjection", camera.viewProjection());
/// @endcode
class Shader {
public:
    /// Compiles and links a program from GLSL source code.
    /// @throws std::runtime_error with the driver's info log if compiling or linking fails.
    Shader(std::string_view vertexSource, std::string_view fragmentSource);

    /// Loads both stages from files and compiles them.
    /// @throws std::runtime_error if a file cannot be read or the program does not compile/link.
    [[nodiscard]] static Shader fromFiles(const std::filesystem::path& vertexPath,
                                          const std::filesystem::path& fragmentPath);

    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    /// Takes over the program of @p other, which becomes empty.
    Shader(Shader&& other) noexcept;
    /// Releases the current program and takes over the one of @p other.
    Shader& operator=(Shader&& other) noexcept;

    /// Makes this program current (`glUseProgram`).
    void bind() const;

    /// @name Uniforms
    /// Each setter binds the program first.
    /// @{
    void setInt(std::string_view name, int value); ///< `int` / `sampler2D`
    void setIntArray(std::string_view name,
                     std::span<const int> values);               ///< `int[]` / `sampler2D[]`
    void setFloat(std::string_view name, float value);           ///< `float`
    void setVec2(std::string_view name, glm::vec2 value);        ///< `vec2`
    void setVec3(std::string_view name, glm::vec3 value);        ///< `vec3`
    void setVec4(std::string_view name, glm::vec4 value);        ///< `vec4`
    void setMat4(std::string_view name, const glm::mat4& value); ///< `mat4`
    /// @}

    /// The OpenGL program name, for direct GL calls.
    [[nodiscard]] std::uint32_t id() const { return m_program; }

private:
    // Lets the cache be searched with a string_view without allocating a std::string.
    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view text) const noexcept
        {
            return std::hash<std::string_view>{}(text);
        }
    };

    [[nodiscard]] int location(std::string_view name);

    std::uint32_t m_program = 0;
    std::unordered_map<std::string, int, StringHash, std::equal_to<>> m_locations;
};

} // namespace astra2d

/// @}
