#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

/// @addtogroup renderer
/// @{

namespace astra2d {

/// What a Buffer holds.
enum class BufferType {
    Vertex, ///< Vertex attributes (`GL_ARRAY_BUFFER`).
    Index,  ///< 32-bit vertex indices (`GL_ELEMENT_ARRAY_BUFFER`).
};

/// How often the contents of a Buffer change. A hint for the driver.
enum class BufferUsage {
    Static,  ///< Written once, drawn many times (`GL_STATIC_DRAW`).
    Dynamic, ///< Rewritten often, e.g. every frame (`GL_DYNAMIC_DRAW`).
};

/// A block of GPU memory for vertices or indices. Owns the OpenGL buffer object; move-only.
class Buffer {
public:
    /// Allocates @p sizeBytes of uninitialized storage, to be filled later with setData().
    Buffer(BufferType type, std::size_t sizeBytes, BufferUsage usage = BufferUsage::Dynamic);

    /// Allocates storage and uploads @p data.
    Buffer(BufferType type, std::span<const std::byte> data,
           BufferUsage usage = BufferUsage::Static);

    ~Buffer();
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    /// Takes over the buffer of @p other, which becomes empty.
    Buffer(Buffer&& other) noexcept;
    /// Releases the current buffer and takes over the one of @p other.
    Buffer& operator=(Buffer&& other) noexcept;

    /// Overwrites part of the buffer, starting @p offsetBytes from its start. The data must fit.
    void setData(std::span<const std::byte> data, std::size_t offsetBytes = 0);

    /// Discards the contents and keeps the size. The driver can then hand out fresh memory
    /// instead of waiting until earlier draw calls have finished reading the old data. Call it
    /// before overwriting a whole dynamic buffer that is rewritten several times per frame.
    void orphan();

    /// Binds the buffer to its target (`GL_ARRAY_BUFFER` or `GL_ELEMENT_ARRAY_BUFFER`).
    void bind() const;

    /// Vertex or index buffer.
    [[nodiscard]] BufferType type() const { return m_type; }
    /// Allocated size in bytes.
    [[nodiscard]] std::size_t size() const { return m_size; }
    /// The OpenGL buffer name, for direct GL calls.
    [[nodiscard]] std::uint32_t id() const { return m_buffer; }

private:
    std::uint32_t m_buffer = 0;
    BufferType m_type;
    BufferUsage m_usage;
    std::size_t m_size = 0;
};

} // namespace astra2d

/// @}
