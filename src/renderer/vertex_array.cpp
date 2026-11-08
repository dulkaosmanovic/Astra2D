#include "astra2d/renderer/vertex_array.hpp"

#include "astra2d/core/assert.hpp"

#include <glad/gl.h>

#include <utility>

namespace astra2d {

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
