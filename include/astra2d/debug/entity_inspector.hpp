#pragma once

#include "astra2d/ecs/entity.hpp"
#include "astra2d/ecs/registry.hpp"

#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

/// @addtogroup debug
/// @{

namespace astra2d {

class Scene;

/// ImGui widgets for a Scene: a list of its entities (as the parent/child hierarchy, or filtered by
/// name) and an inspector that shows and edits the components of the selected one. Part of the
/// DebugOverlay; draw() can also be called inside any ImGui window.
///
/// The engine's components (Name, Transform, SpriteRenderer, Animator, Lifetime, RigidBody,
/// Collider, TilemapCollider, Relationship, Interpolated) have inspectors built in. The registry
/// does not know component names, so a game adds its own with addInspector():
///
/// @code
/// overlay.inspector().addInspector<Coin>("Coin", [](Coin& coin) {
///     ImGui::DragInt("Value", &coin.value);
/// });
/// @endcode
///
/// Changes take effect like any other change by the game, e.g. a moved Transform or a new
/// RigidBody velocity reach the physics at its next step.
class EntityInspector {
public:
    /// Draws the widgets of a component; called only when the entity has it.
    using Inspect = std::function<void(Registry& registry, Entity entity)>;
    /// True if the entity has the component.
    using Has = std::function<bool(const Registry& registry, Entity entity)>;

    /// An inspector with the engine's components registered.
    EntityInspector();

    /// Shows @p Component as @p name; @p inspect gets the component (`void(Component&)`) and
    /// draws ImGui widgets for it. Inspectors appear in the order they were added.
    template <typename Component, typename Function>
    void addInspector(std::string name, Function inspect)
    {
        addInspector(std::move(name), [](const Registry& registry, Entity entity) {
            return registry.has<Component>(entity);
        }, [inspect = std::move(inspect)](Registry& registry, Entity entity) mutable {
            inspect(registry.get<Component>(entity));
        });
    }

    /// Shows a component called @p name for the entities where @p has is true, with the widgets
    /// drawn by @p inspect. The type-erased form of addInspector<Component>().
    void addInspector(std::string name, Has has, Inspect inspect);

    /// Names of the inspected components, in display order.
    [[nodiscard]] std::vector<std::string> componentNames() const;

    /// Draws the entity list and, below it, the selected entity's components, into the current
    /// ImGui window. A selected entity that was destroyed is deselected.
    void draw(Scene& scene);

    /// Selects @p entity (the null entity: none). The next draw() shows it in the list: its
    /// parents are expanded, the filter is cleared and the list scrolls to it.
    void select(Entity entity)
    {
        m_selected = entity;
        m_reveal = !entity.isNull();
    }
    /// The selected entity, or the null entity. It may have been destroyed since; draw() checks.
    [[nodiscard]] Entity selected() const { return m_selected; }

private:
    struct ComponentInspector {
        std::string name;
        Has has;
        Inspect inspect;
    };

    void drawList(Scene& scene);
    void drawSelected(Scene& scene);

    std::vector<ComponentInspector> m_inspectors;
    Entity m_selected;
    std::unordered_set<Entity> m_expanded; // entities whose children are listed
    std::string m_filter;
    bool m_reveal = false; // select() asked to show the selection in the list
};

} // namespace astra2d

/// @}
