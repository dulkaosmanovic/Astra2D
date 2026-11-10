#pragma once

// An ImGui context without a window or renderer, for testing widgets: frames are built and
// checked by ImGui's own assertions (IM_ASSERT, active in non-release builds), never drawn.

#include <imgui.h>

#include <utility>

class HeadlessImGui {
public:
    HeadlessImGui()
        : m_context(ImGui::CreateContext())
    {
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0F, 720.0F);
        io.DeltaTime = 1.0F / 60.0F;
        // ImGui then asks the renderer to create the font texture instead of needing it built
        // up front; there is no renderer, and nothing is drawn.
        io.BackendFlags = ImGuiBackendFlags_RendererHasTextures; // the only one
    }
    ~HeadlessImGui() { ImGui::DestroyContext(m_context); }

    HeadlessImGui(const HeadlessImGui&) = delete;
    HeadlessImGui& operator=(const HeadlessImGui&) = delete;
    HeadlessImGui(HeadlessImGui&&) = delete;
    HeadlessImGui& operator=(HeadlessImGui&&) = delete;

    /// One frame with @p widgets drawn inside a window.
    template <typename Function>
    void frame(Function&& widgets)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(400.0F, 700.0F));
        ImGui::Begin("test");
        std::forward<Function>(widgets)();
        ImGui::End();
        ImGui::Render();
    }

private:
    ImGuiContext* m_context;
};
