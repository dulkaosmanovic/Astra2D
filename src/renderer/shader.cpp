#include "astra2d/renderer/shader.hpp"

#include "astra2d/core/log.hpp"

#include <glad/gl.h>

#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace astra2d {

static_assert(std::is_same_v<GLuint, std::uint32_t>);

namespace {

std::string readFile(const std::filesystem::path& path)
{
    const std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::format("Cannot open shader file '{}'", path.string()));
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

const char* stageName(GLenum type)
{
    return type == GL_VERTEX_SHADER ? "vertex" : "fragment";
}

GLuint compile(GLenum type, std::string_view source)
{
    const GLuint shader = glCreateShader(type);
    const GLchar* text = source.data();
    const auto length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        std::array<char, 1024> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error(
            std::format("Failed to compile {} shader:\n{}", stageName(type), log.data()));
    }
    return shader;
}

} // namespace

Shader::Shader(std::string_view vertexSource, std::string_view fragmentSource)
{
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource);
    GLuint fragment = 0;
    try {
        fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
    } catch (...) {
        glDeleteShader(vertex);
        throw;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vertex);
    glAttachShader(m_program, fragment);
    glLinkProgram(m_program);
    // The program keeps the compiled code; the shader objects are no longer needed.
    glDetachShader(m_program, vertex);
    glDetachShader(m_program, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        std::array<char, 1024> log{};
        glGetProgramInfoLog(m_program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        glDeleteProgram(m_program);
        m_program = 0;
        throw std::runtime_error(std::format("Failed to link shader program:\n{}", log.data()));
    }
}

Shader Shader::fromFiles(const std::filesystem::path& vertexPath,
                         const std::filesystem::path& fragmentPath)
{
    try {
        return {readFile(vertexPath), readFile(fragmentPath)};
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(std::format("{} (shader '{}' + '{}')", error.what(),
                                             vertexPath.string(), fragmentPath.string()));
    }
}

Shader::~Shader()
{
    glDeleteProgram(m_program); // 0 is ignored
}

Shader::Shader(Shader&& other) noexcept
    : m_program(std::exchange(other.m_program, 0))
    , m_locations(std::move(other.m_locations))
{
}

Shader& Shader::operator=(Shader&& other) noexcept
{
    if (this != &other) {
        glDeleteProgram(m_program);
        m_program = std::exchange(other.m_program, 0);
        m_locations = std::move(other.m_locations);
    }
    return *this;
}

void Shader::bind() const
{
    glUseProgram(m_program);
}

int Shader::location(std::string_view name)
{
    if (const auto it = m_locations.find(name); it != m_locations.end()) {
        return it->second;
    }
    const std::string key(name);
    const GLint location = glGetUniformLocation(m_program, key.c_str());
    if (location < 0) {
        ASTRA_CORE_DEBUG("Shader {}: uniform '{}' not found (unused or misspelled)", m_program,
                         key);
    }
    m_locations.emplace(key, location);
    return location;
}

void Shader::setInt(std::string_view name, int value)
{
    bind();
    glUniform1i(location(name), value);
}

void Shader::setIntArray(std::string_view name, std::span<const int> values)
{
    bind();
    glUniform1iv(location(name), static_cast<GLsizei>(values.size()), values.data());
}

void Shader::setFloat(std::string_view name, float value)
{
    bind();
    glUniform1f(location(name), value);
}

void Shader::setVec2(std::string_view name, glm::vec2 value)
{
    bind();
    glUniform2f(location(name), value.x, value.y);
}

void Shader::setVec3(std::string_view name, glm::vec3 value)
{
    bind();
    glUniform3f(location(name), value.x, value.y, value.z);
}

void Shader::setVec4(std::string_view name, glm::vec4 value)
{
    bind();
    glUniform4f(location(name), value.x, value.y, value.z, value.w);
}

void Shader::setMat4(std::string_view name, const glm::mat4& value)
{
    bind();
    glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}

} // namespace astra2d
