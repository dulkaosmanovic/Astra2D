#include "astra2d/renderer/buffer.hpp"

#include "astra2d/core/assert.hpp"

#include <glad/gl.h>

#include <utility>

namespace astra2d {

namespace {

GLenum target(BufferType type)
{
    return type == BufferType::Vertex ? GL_ARRAY_BUFFER : GL_ELEMENT_ARRAY_BUFFER;
}

GLenum glUsage(BufferUsage usage)
{
    return usage == BufferUsage::Static ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW;
}

// All uploads go through GL_COPY_WRITE_BUFFER, a binding point used for nothing else. Binding an
// index buffer to GL_ELEMENT_ARRAY_BUFFER would change whichever vertex array is bound (and is an
// error on strict core-profile drivers when none is), and binding to GL_ARRAY_BUFFER would disturb
// code that relies on that binding. Buffers are untyped in OpenGL, so this works for both types.
constexpr GLenum kUploadTarget = GL_COPY_WRITE_BUFFER;

} // namespace

// ---------------------------------------------------------------------------------------------
// Buffer

Buffer::Buffer(BufferType type, std::size_t sizeBytes, BufferUsage usage)
    : m_type(type)
    , m_usage(usage)
    , m_size(sizeBytes)
{
    glGenBuffers(1, &m_buffer);
    glBindBuffer(kUploadTarget, m_buffer);
    glBufferData(kUploadTarget, static_cast<GLsizeiptr>(sizeBytes), nullptr, glUsage(usage));
}

Buffer::Buffer(BufferType type, std::span<const std::byte> data, BufferUsage usage)
    : m_type(type)
    , m_usage(usage)
    , m_size(data.size())
{
    glGenBuffers(1, &m_buffer);
    glBindBuffer(kUploadTarget, m_buffer);
    glBufferData(kUploadTarget, static_cast<GLsizeiptr>(data.size()), data.data(), glUsage(usage));
}

Buffer::~Buffer()
{
    glDeleteBuffers(1, &m_buffer); // 0 is ignored
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_buffer(std::exchange(other.m_buffer, 0))
    , m_type(other.m_type)
    , m_usage(other.m_usage)
    , m_size(std::exchange(other.m_size, 0))
{
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    if (this != &other) {
        glDeleteBuffers(1, &m_buffer);
        m_buffer = std::exchange(other.m_buffer, 0);
        m_type = other.m_type;
        m_usage = other.m_usage;
        m_size = std::exchange(other.m_size, 0);
    }
    return *this;
}

void Buffer::setData(std::span<const std::byte> data, std::size_t offsetBytes)
{
    ASTRA_ASSERT(offsetBytes + data.size() <= m_size, "Buffer overflow: {} + {} bytes > {}",
                 offsetBytes, data.size(), m_size);
    glBindBuffer(kUploadTarget, m_buffer);
    glBufferSubData(kUploadTarget, static_cast<GLintptr>(offsetBytes),
                    static_cast<GLsizeiptr>(data.size()), data.data());
}

void Buffer::orphan()
{
    glBindBuffer(kUploadTarget, m_buffer);
    glBufferData(kUploadTarget, static_cast<GLsizeiptr>(m_size), nullptr, glUsage(m_usage));
}

void Buffer::bind() const
{
    glBindBuffer(target(m_type), m_buffer);
}

} // namespace astra2d
