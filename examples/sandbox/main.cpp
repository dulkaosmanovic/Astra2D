#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>

#include <glad/gl.h>

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

GLuint compileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::array<char, 512> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        ASTRA_ERROR("Shader compilation failed: {}", log.data());
    }
    return shader;
}

} // namespace

// Temporary sandbox: draws a triangle with the OpenGL 3.3 core pipeline.
// Raw GL calls will be replaced by the Renderer2D API once it exists.
//
// Controls: arrow keys / WASD move the triangle, Space resets it, Escape quits.
class Sandbox : public astra2d::Application {
public:
    Sandbox()
        : Application({ .window = { .title = "Astra2D Sandbox", .width = 800, .height = 600 } })
    {
    }

protected:
    void onStart() override
    {
        constexpr std::array<float, 15> vertices = {
            // position     // color
             0.0f,  0.5f,   1.0f, 0.3f, 0.3f,
            -0.5f, -0.5f,   0.3f, 1.0f, 0.3f,
             0.5f, -0.5f,   0.3f, 0.3f, 1.0f,
        };

        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                              reinterpret_cast<void*>(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShader);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
        m_program = glCreateProgram();
        glAttachShader(m_program, vs);
        glAttachShader(m_program, fs);
        glLinkProgram(m_program);
        glDeleteShader(vs);
        glDeleteShader(fs);

        m_offsetLocation = glGetUniformLocation(m_program, "uOffset");
        m_aspectLocation = glGetUniformLocation(m_program, "uAspect");
    }

    void onFixedUpdate(float dt) override
    {
        // Held state is safe to read here; it does not depend on the frame/step ratio.
        const auto& in = input();
        float dx = 0.0f;
        float dy = 0.0f;
        if (in.isHeld(astra2d::Key::Left) || in.isHeld(astra2d::Key::A)) dx -= 1.0f;
        if (in.isHeld(astra2d::Key::Right) || in.isHeld(astra2d::Key::D)) dx += 1.0f;
        if (in.isHeld(astra2d::Key::Down) || in.isHeld(astra2d::Key::S)) dy -= 1.0f;
        if (in.isHeld(astra2d::Key::Up) || in.isHeld(astra2d::Key::W)) dy += 1.0f;

        constexpr float kSpeed = 1.0f; // units per second
        m_previousOffset = m_offset;
        m_offset[0] += dx * kSpeed * dt;
        m_offset[1] += dy * kSpeed * dt;
    }

    void onUpdate(float dt) override
    {
        // One-shot events (pressed/released) are read once per frame.
        if (input().isPressed(astra2d::Key::Escape)) {
            quit();
        }
        if (input().isPressed(astra2d::Key::Space)) {
            m_offset = {};
            m_previousOffset = {};
            ASTRA_INFO("Position reset");
        }
        if (input().isPressed(astra2d::MouseButton::Left)) {
            const auto pos = input().mousePosition();
            ASTRA_INFO("Click at ({:.0f}, {:.0f})", pos.x, pos.y);
        }

        m_fpsTimer += dt;
        ++m_fpsFrames;
        if (m_fpsTimer >= 1.0f) {
            window().setTitle(std::format("Astra2D Sandbox | {} FPS | {:.2f} ms",
                                          m_fpsFrames, 1000.0f * m_fpsTimer / static_cast<float>(m_fpsFrames)));
            m_fpsTimer = 0.0f;
            m_fpsFrames = 0;
        }
    }

    void onResize(int width, int height) override
    {
        ASTRA_INFO("Resized to {}x{}", width, height);
    }

    void onRender() override
    {
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Interpolate between the last two fixed steps for smooth motion at any frame rate.
        const float alpha = interpolationAlpha();
        const float x = m_previousOffset[0] + (m_offset[0] - m_previousOffset[0]) * alpha;
        const float y = m_previousOffset[1] + (m_offset[1] - m_previousOffset[1]) * alpha;

        glUseProgram(m_program);
        glUniform2f(m_offsetLocation, x, y);
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

    std::array<float, 2> m_offset{};
    std::array<float, 2> m_previousOffset{};

    float m_fpsTimer = 0.0f;
    int m_fpsFrames = 0;
};

int main()
{
    Sandbox sandbox;
    sandbox.run();
}
