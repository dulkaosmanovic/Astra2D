#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>

#include <glad/gl.h>

#include <glm/common.hpp>
#include <glm/vec2.hpp>

#include <array>
#include <format>

namespace {

constexpr const char* kVertexShader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aColor;
uniform vec2 uOffset;
uniform float uAspect;
out vec3 vColor;
void main()
{
    vColor = aColor;
    vec2 pos = aPos + uOffset;
    gl_Position = vec4(pos.x / uAspect, pos.y, 0.0, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main()
{
    FragColor = vec4(vColor, 1.0);
}
)";

// Returns 0 if the shader does not compile.
GLuint compileShader(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        std::array<char, 512> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        ASTRA_ERROR("Shader compilation failed: {}", log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Returns 0 if either shader or the link step fails.
GLuint createProgram(const char* vertexSource, const char* fragmentSource)
{
    const GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vs == 0 || fs == 0) {
        glDeleteShader(vs); // deleting 0 is a no-op
        glDeleteShader(fs);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        std::array<char, 512> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        ASTRA_ERROR("Shader program link failed: {}", log.data());
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

// Temporary sandbox: draws a triangle with the OpenGL 3.3 core pipeline.
// Raw GL calls will be replaced by the Renderer2D API once it exists.
//
// Controls: arrow keys / WASD move the triangle, Space resets it, Escape quits.
class Sandbox : public astra2d::Application {
public:
    Sandbox()
        : Application({.window = {.title = "Astra2D Sandbox", .width = 800, .height = 600}})
    {
    }

protected:
    void onStart() override
    {
        // clang-format off
        constexpr std::array<float, 15> vertices = {
            // position     // color
             0.0F,  0.5F,   1.0F, 0.3F, 0.3F,
            -0.5F, -0.5F,   0.3F, 1.0F, 0.3F,
             0.5F, -0.5F,   0.3F, 0.3F, 1.0F,
        };
        // clang-format on

        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
            // NOLINTNEXTLINE(*-reinterpret-cast,performance-no-int-to-ptr): GL API
            reinterpret_cast<void*>(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        m_program = createProgram(kVertexShader, kFragmentShader);
        if (m_program == 0) {
            ASTRA_CRITICAL("Cannot continue without the shader program");
            quit();
            return;
        }

        m_offsetLocation = glGetUniformLocation(m_program, "uOffset");
        m_aspectLocation = glGetUniformLocation(m_program, "uAspect");
    }

    void onFixedUpdate(float dt) override
    {
        // Held state is safe to read here; it does not depend on the frame/step ratio.
        const auto& in = input();
        glm::vec2 direction{0.0F};
        if (in.isHeld(astra2d::Key::Left) || in.isHeld(astra2d::Key::A)) {
            direction.x -= 1.0F;
        }
        if (in.isHeld(astra2d::Key::Right) || in.isHeld(astra2d::Key::D)) {
            direction.x += 1.0F;
        }
        if (in.isHeld(astra2d::Key::Down) || in.isHeld(astra2d::Key::S)) {
            direction.y -= 1.0F;
        }
        if (in.isHeld(astra2d::Key::Up) || in.isHeld(astra2d::Key::W)) {
            direction.y += 1.0F;
        }

        constexpr float kSpeed = 1.0F; // units per second
        m_previousOffset = m_offset;
        m_offset += direction * (kSpeed * dt);
    }

    void onUpdate(float dt) override
    {
        // One-shot events (pressed/released) are read once per frame.
        if (input().isPressed(astra2d::Key::Escape)) {
            quit();
        }
        if (input().isPressed(astra2d::Key::Space)) {
            m_offset = glm::vec2{0.0F};
            m_previousOffset = glm::vec2{0.0F};
            ASTRA_INFO("Position reset");
        }
        if (input().isPressed(astra2d::MouseButton::Left)) {
            const auto pos = input().mousePosition();
            ASTRA_INFO("Click at ({:.0f}, {:.0f})", pos.x, pos.y);
        }

        m_fpsTimer += dt;
        ++m_fpsFrames;
        if (m_fpsTimer >= 1.0F) {
            window().setTitle(std::format("Astra2D Sandbox | {} FPS | {:.2f} ms", m_fpsFrames,
                                          1000.0F * m_fpsTimer / static_cast<float>(m_fpsFrames)));
            m_fpsTimer = 0.0F;
            m_fpsFrames = 0;
        }
    }

    void onResize(int width, int height) override { ASTRA_INFO("Resized to {}x{}", width, height); }

    void onRender() override
    {
        glClearColor(0.1F, 0.1F, 0.12F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        // Interpolate between the last two fixed steps for smooth motion at any frame rate.
        const float alpha = interpolationAlpha();
        const glm::vec2 position = glm::mix(m_previousOffset, m_offset, alpha);

        glUseProgram(m_program);
        glUniform2f(m_offsetLocation, position.x, position.y);
        glUniform1f(m_aspectLocation, window().aspectRatio());
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    void onShutdown() override
    {
        glDeleteProgram(m_program);
        glDeleteBuffers(1, &m_vbo);
        glDeleteVertexArrays(1, &m_vao);
    }

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_program = 0;
    GLint m_offsetLocation = -1;
    GLint m_aspectLocation = -1;

    glm::vec2 m_offset{0.0F};
    glm::vec2 m_previousOffset{0.0F};

    float m_fpsTimer = 0.0F;
    int m_fpsFrames = 0;
};

} // namespace

int main()
{
    Sandbox sandbox;
    sandbox.run();
}
