#pragma once

// Small helpers for the debug module's ImGui code:
// - formatted text without C varargs (ImGui::Text takes printf arguments, which are not type
//   checked): the text is made with std::format and drawn unformatted;
// - ImGui's flags are plain int bit masks; combining them through unsigned keeps the bit
//   operations well defined.

#include <imgui.h>

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace astra2d::detail {

template <typename... Args>
void text(std::format_string<Args...> format, Args&&... args)
{
    const std::string line = std::format(format, std::forward<Args>(args)...);
    ImGui::TextUnformatted(line.data(), line.data() + line.size());
}

/// Text in the colour of disabled widgets, for hints.
inline void hint(std::string_view line)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(line.data(), line.data() + line.size());
    ImGui::PopStyleColor();
}

/// The union of ImGui flags of one kind, e.g. `flags(ImGuiTreeNodeFlags_Leaf, ...)`.
template <typename... Flags>
[[nodiscard]] constexpr int flags(Flags... values)
{
    return static_cast<int>((0U | ... | static_cast<unsigned>(values)));
}

} // namespace astra2d::detail
