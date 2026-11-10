#include "entity_rows.hpp"

#include "astra2d/scene/components.hpp"
#include "astra2d/scene/scene.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>

namespace astra2d::detail {

namespace {

bool containsIgnoringCase(std::string_view text, std::string_view part)
{
    const auto lower = [](char c) { return std::tolower(static_cast<unsigned char>(c)); };
    return !std::ranges::search(text, part, {}, lower, lower).empty();
}

} // namespace

std::string entityLabel(const Registry& registry, Entity entity)
{
    const auto* name = registry.tryGet<Name>(entity);
    if (name == nullptr || name->value.empty()) {
        return std::format("#{}", entity.index());
    }
    return std::format("{} #{}", name->value, entity.index());
}

std::vector<EntityRow> entityRows(const Scene& scene, const std::unordered_set<Entity>& expanded,
                                  std::string_view filter)
{
    const Registry& registry = scene.registry();
    std::vector<EntityRow> rows;
    if (!filter.empty()) {
        registry.forEach([&](Entity entity) {
            if (containsIgnoringCase(entityLabel(registry, entity), filter)) {
                rows.push_back({.entity = entity});
            }
        });
        return rows;
    }

    std::vector<EntityRow> stack; // depth-first: the next row is on top
    registry.forEach([&](Entity root) {
        if (!scene.parent(root).isNull()) {
            return;
        }
        stack.push_back({.entity = root});
        while (!stack.empty()) {
            EntityRow row = stack.back();
            stack.pop_back();
            const std::vector<Entity> children = scene.children(row.entity);
            row.hasChildren = !children.empty();
            rows.push_back(row);
            if (row.hasChildren && expanded.contains(row.entity)) {
                for (const Entity child : std::views::reverse(children)) {
                    stack.push_back({.entity = child, .depth = row.depth + 1});
                }
            }
        }
    });
    return rows;
}

} // namespace astra2d::detail
