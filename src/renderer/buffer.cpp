#include "astra2d/renderer/buffer.hpp"
#include "astra2d/renderer/vertex_array.hpp"

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

// ---------------------------------------------------------------------------------------------
// VertexLayout

VertexLayout::VertexLayout(std::initializer_list<VertexAttribute> attributes)
    : m_attributes(attributes)
{
    for (const VertexAttribute& attribute : m_attributes) {
        ASTRA_ASSERT(attribute.components >= 1 && attribute.components <= 4,
                     "Vertex attributes have 1 to 4 components, got {}", attribute.components);
        m_stride += static_cast<std::size_t>(attribute.components) * 4U; // float and int: 4 bytes
    }
}

std::size_t VertexLayout::offset(std::size_t index) const
{
    ASTRA_ASSERT(index < m_attributes.size());
    std::size_t offset = 0;
    for (std::size_t i = 0; i < index; ++i) {
        offset += static_cast<std::size_t>(m_attributes[i].components) * 4U;
    }
    return offset;
}

// ---------------------------------------------------------------------------------------------
// VertexArray

VertexArray::VertexArray()
{
    glGenVertexArrays(1, &m_vao);
}

VertexArray::~VertexArray()
{
    glDeleteVertexArrays(1, &m_vao); // 0 is ignored
}

VertexArray::VertexArray(VertexArray&& other) noexcept
    : m_vao(std::exchange(other.m_vao, 0))
{
}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept
{
    if (this != &other) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = std::exchange(other.m_vao, 0);
    }
    return *this;
}

// Not const: the methods change the vertex array object on the GPU through the handle.
// NOLINTNEXTLINE(readability-make-member-function-const)
void VertexArray::setVertexBuffer(const Buffer& buffer, const VertexLayout& layout)
{
    ASTRA_ASSERT(buffer.type() == BufferType::Vertex);
    glBindVertexArray(m_vao);
    buffer.bind();

    const auto stride = static_cast<GLsizei>(layout.stride());
    const auto& attributes = layout.attributes();
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const auto location = static_cast<GLuint>(i);
        // OpenGL takes the byte offset into the bound buffer disguised as a pointer.
        // NOLINTNEXTLINE(*-reinterpret-cast,performance-no-int-to-ptr): GL API
        const auto* offset = reinterpret_cast<const void*>(layout.offset(i));
        glEnableVertexAttribArray(location);
        if (attributes[i].type == AttributeType::Int) {
            glVertexAttribIPointer(location, attributes[i].components, GL_INT, stride, offset);
        } else {
            glVertexAttribPointer(location, attributes[i].components, GL_FLOAT, GL_FALSE, stride,
                                  offset);
        }
    }
    glBindVertexArray(0);
}

// NOLINTNEXTLINE(readability-make-member-function-const): changes the VAO on the GPU
void VertexArray::setIndexBuffer(const Buffer& buffer)
{
    ASTRA_ASSERT(buffer.type() == BufferType::Index);
    glBindVertexArray(m_vao);
    buffer.bind(); // recorded in the VAO
    glBindVertexArray(0);
}

void VertexArray::bind() const
{
    glBindVertexArray(m_vao);
}

} // namespace astra2d
