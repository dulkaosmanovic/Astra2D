#pragma once

#include "astra2d/renderer/buffer.hpp"

#include <cstdint>
#include <initializer_list>
#include <vector>

/// @addtogroup renderer
/// @{

namespace astra2d {

/// Data type of one vertex attribute.
enum class AttributeType {
    Float, ///< Read as `float`/`vecN` in the shader.
    Int,   ///< Read as `int`/`ivecN` in the shader (no conversion to float).
};

/// One attribute of a vertex, e.g. a `vec2` position.
struct VertexAttribute {
    int components = 1;                        ///< 1 to 4.
    AttributeType type = AttributeType::Float; ///< Component type (32-bit).
};

/// Describes how the vertices in a buffer are laid out: attributes are tightly packed in the
/// listed order, and attribute *i* is bound to shader `location = i`.
///
/// @code
/// const VertexLayout layout{{2}, {4}, {2}}; // vec2 position, vec4 color, vec2 uv
/// @endcode
class VertexLayout {
public:
    /// Builds the layout from its attributes, in shader location order.
    VertexLayout(std::initializer_list<VertexAttribute> attributes);

    /// The attributes in order.
    [[nodiscard]] const std::vector<VertexAttribute>& attributes() const { return m_attributes; }
    /// Size of one vertex in bytes.
    [[nodiscard]] std::size_t stride() const { return m_stride; }
    /// Offset of attribute @p index from the start of a vertex, in bytes.
    [[nodiscard]] std::size_t offset(std::size_t index) const;

private:
    std::vector<VertexAttribute> m_attributes;
    std::size_t m_stride = 0;
};

/// A vertex array object: remembers which buffers and layout a draw call uses. Move-only.
///
/// The buffers are not owned and must outlive the VertexArray (or stay unused while it exists).
class VertexArray {
public:
    VertexArray();
    ~VertexArray();
    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;
    /// Takes over the vertex array of @p other, which becomes empty.
    VertexArray(VertexArray&& other) noexcept;
    /// Releases the current vertex array and takes over the one of @p other.
    VertexArray& operator=(VertexArray&& other) noexcept;

    /// Attaches a vertex buffer with the given layout, starting at shader location 0.
    void setVertexBuffer(const Buffer& buffer, const VertexLayout& layout);
    /// Attaches the index buffer used by indexed draw calls.
    void setIndexBuffer(const Buffer& buffer);

    /// Binds this vertex array (`glBindVertexArray`).
    void bind() const;

    /// The OpenGL vertex array name, for direct GL calls.
    [[nodiscard]] std::uint32_t id() const { return m_vao; }

private:
    std::uint32_t m_vao = 0;
};

} // namespace astra2d

/// @}
