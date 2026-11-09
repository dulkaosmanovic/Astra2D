#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

/// @addtogroup renderer
/// @{

namespace astra2d {

/// A 2D camera with an orthographic projection. The world is y-up.
///
/// The camera shows viewHeight() / zoom() world units vertically, centred on position() and turned
/// by rotation(); the visible width follows the aspect ratio of the viewport. With the default view
/// height of 720 and a 1280x720 window, one world unit is one pixel.
///
/// The viewport size must be kept up to date, usually from Application::onResize():
/// @code
/// void onResize(int width, int height) override { m_camera.setViewportSize(width, height); }
/// @endcode
///
/// The class does not use OpenGL, so it can be unit-tested and used in headless code.
class OrthographicCamera {
public:
    /// @param viewHeight World units visible vertically at zoom 1 (must be > 0).
    /// @param viewport   Viewport size in pixels; see setViewportSize().
    explicit OrthographicCamera(float viewHeight = 720.0F, glm::ivec2 viewport = {1280, 720});

    /// A camera in which one world unit is one pixel of a @p width x @p height viewport and (0, 0)
    /// is its bottom-left corner. Use it for HUD and text that stay fixed on the screen:
    /// @code
    /// renderer.begin(OrthographicCamera::screenSpace(window().framebufferWidth(),
    ///                                                window().framebufferHeight()));
    /// @endcode
    [[nodiscard]] static OrthographicCamera screenSpace(int width, int height);

    /// Sets the viewport (framebuffer) size in pixels. Sizes below 1 (a minimized window) are
    /// ignored, so the last valid aspect ratio is kept.
    void setViewportSize(int width, int height);
    /// Viewport size in pixels.
    [[nodiscard]] glm::ivec2 viewportSize() const { return m_viewport; }

    /// Moves the camera so that @p position is at the centre of the viewport.
    void setPosition(glm::vec2 position);
    /// World position at the centre of the viewport.
    [[nodiscard]] glm::vec2 position() const { return m_position; }

    /// Sets the zoom factor (must be > 0). 2 shows half as much of the world, twice as large.
    void setZoom(float zoom);
    /// Current zoom factor.
    [[nodiscard]] float zoom() const { return m_zoom; }

    /// Turns the camera by @p rotation radians, counter-clockwise (the world appears to turn
    /// clockwise). Non-finite values are treated as 0.
    void setRotation(float rotation);
    /// Camera rotation in radians.
    [[nodiscard]] float rotation() const { return m_rotation; }

    /// A copy of this camera for a parallax layer: its position is multiplied by @p factor. A
    /// factor of 0 keeps the layer fixed on screen (a distant sky), 1 moves it with the world, and
    /// values in between make it scroll slower, so it appears further away.
    [[nodiscard]] OrthographicCamera withParallax(glm::vec2 factor) const;

    /// Sets the world units visible vertically at zoom 1 (must be > 0). Ends fitArea().
    void setViewHeight(float viewHeight);
    /// Keeps at least @p area (width x height world units, both > 0) visible at zoom 1, whatever
    /// the window's aspect ratio: the view height is chosen so the area fits, now and after every
    /// setViewportSize(), and the other direction shows more of the world. Use it when the whole
    /// playing field must stay on screen in wide, tall and tiled windows alike:
    /// @code
    /// m_camera.fitArea({640.0F, 360.0F});   // once; onResize() keeps calling setViewportSize()
    /// @endcode
    void fitArea(glm::vec2 area);
    /// Shows about @p viewHeight world units vertically (> 0) with a whole number of screen pixels
    /// per world unit, now and after every setViewportSize(), and draws from a position rounded to
    /// whole screen pixels, so pixel art stays crisp, without shimmering while the camera moves
    /// (like Unity's Pixel Perfect Camera). With one texel per world unit, a 720 pixel window and
    /// @p viewHeight 224 give 3 pixels per texel and show 240 units. Ends fitArea();
    /// setViewHeight() and fitArea() end it.
    void setPixelPerfect(float viewHeight);
    /// True after setPixelPerfect().
    [[nodiscard]] bool pixelPerfect() const { return m_pixelPerfectHeight > 0.0F; }
    /// Screen pixels per world unit (a whole number after setPixelPerfect() at zoom 1).
    [[nodiscard]] float pixelsPerUnit() const;
    /// World units visible vertically at zoom 1.
    [[nodiscard]] float viewHeight() const { return m_viewHeight; }

    /// Size of the view rectangle in world units (before rotation).
    [[nodiscard]] glm::vec2 visibleSize() const;
    /// Bottom-left corner of the axis-aligned bounds of the visible world (they contain the whole
    /// rotated view, so they suit culling).
    [[nodiscard]] glm::vec2 visibleMin() const;
    /// Top-right corner of the axis-aligned bounds of the visible world.
    [[nodiscard]] glm::vec2 visibleMax() const;

    /// Combined view and projection matrix: maps world coordinates to clip space.
    [[nodiscard]] glm::mat4 viewProjection() const;

    /// Converts a point in framebuffer pixels (origin top-left, y down, as reported by
    /// Input::mousePosition()) to world coordinates.
    [[nodiscard]] glm::vec2 screenToWorld(glm::vec2 screen) const;
    /// Converts world coordinates to framebuffer pixels (origin top-left, y down).
    [[nodiscard]] glm::vec2 worldToScreen(glm::vec2 world) const;

private:
    [[nodiscard]] static glm::vec2 rotate(glm::vec2 v, float angle);
    [[nodiscard]] glm::vec2 visibleHalfExtents() const;

    glm::vec2 m_position{0.0F};
    glm::ivec2 m_viewport;
    void applyFit();

    float m_viewHeight;
    glm::vec2 m_fitArea{0.0F};         // (0, 0): no fitArea()
    float m_pixelPerfectHeight = 0.0F; // 0: no setPixelPerfect()
    float m_zoom = 1.0F;
    float m_rotation = 0.0F;
};

} // namespace astra2d

/// @}
