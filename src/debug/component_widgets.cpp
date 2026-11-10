#include "component_widgets.hpp"

#include "astra2d/debug/entity_inspector.hpp"
#include "astra2d/renderer/texture.hpp"
#include "astra2d/scene/components.hpp"
#include "imgui_helpers.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace astra2d::detail {

namespace {

// ImGui::Combo over an enum whose values are 0, 1, 2, ... in the order of @p names.
template <typename Enum, std::size_t N>
void enumCombo(const char* label, Enum& value, const std::array<const char*, N>& names)
{
    int index = static_cast<int>(value);
    if (ImGui::Combo(label, &index, names.data(), static_cast<int>(N))) {
        value = static_cast<Enum>(index);
    }
}

void hexInput(const char* label, std::uint16_t& bits)
{
    ImGui::InputScalar(label, ImGuiDataType_U16, &bits, nullptr, nullptr, "%04X",
                       ImGuiInputTextFlags_CharsHexadecimal);
}

void inspectName(Name& name)
{
    ImGui::InputText("Name", &name.value);
}

void inspectTransform(Transform& transform)
{
    ImGui::DragFloat2("Position", glm::value_ptr(transform.position), 0.05F);
    ImGui::SliderAngle("Rotation", &transform.rotation, -360.0F, 360.0F);
    ImGui::DragFloat2("Scale", glm::value_ptr(transform.scale), 0.01F);
}

// The sprite's region of its texture, at most 64 pixels high. Texture rows start at the bottom,
// ImGui's at the top, so v is flipped.
void spritePreview(const Sprite& sprite)
{
    const Texture2D& texture = *sprite.texture;
    const glm::vec2 uvSize = glm::abs(sprite.region.uvMax - sprite.region.uvMin);
    const glm::vec2 texels = uvSize * glm::vec2(static_cast<float>(texture.width()),
                                                static_cast<float>(texture.height()));
    const float scale = texels.y > 0.0F ? std::min(64.0F / texels.y, 4.0F) : 1.0F;
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(texture.id())),
                 ImVec2(texels.x * scale, texels.y * scale),
                 ImVec2(sprite.region.uvMin.x, sprite.region.uvMax.y),
                 ImVec2(sprite.region.uvMax.x, sprite.region.uvMin.y));
    text("{} x {} texture, {:.0f} x {:.0f} texels", texture.width(), texture.height(), texels.x,
         texels.y);
}

void inspectSpriteRenderer(SpriteRenderer& sprite)
{
    if (sprite.sprite.texture != nullptr) {
        spritePreview(sprite.sprite);
    } else {
        text("No texture (solid colour)");
    }
    ImGui::DragFloat2("Size", glm::value_ptr(sprite.size), 0.05F);
    ImGui::ColorEdit4("Tint", glm::value_ptr(sprite.tint));
    ImGui::InputInt("Layer", &sprite.layer);
    ImGui::Checkbox("Flip X", &sprite.flipX);
    ImGui::SameLine();
    ImGui::Checkbox("Visible", &sprite.visible);
}

void inspectAnimator(Animator& animator)
{
    AnimationPlayer& player = animator.player;
    if (const SpriteAnimation* animation = player.animation()) {
        text("Frame {} ({} of {}){}", player.frame(), player.frameIndex() + 1,
             animation->frames.size(), player.finished() ? ", finished" : "");
        text("{:.3f} s per frame, {}", animation->frameDuration,
             animation->loop ? "looping" : "once");
    } else {
        text("Not playing");
    }
    float speed = player.speed();
    if (ImGui::DragFloat("Speed", &speed, 0.01F, 0.0F, 10.0F)) {
        player.setSpeed(speed);
    }
    ImGui::Checkbox("Destroy when finished", &animator.destroyWhenFinished);
}

void inspectLifetime(Lifetime& lifetime)
{
    ImGui::ProgressBar(lifetime.progress());
    ImGui::DragFloat("Duration", &lifetime.duration, 0.01F, 0.0F, 1000.0F);
    ImGui::DragFloat("Age", &lifetime.age, 0.01F, 0.0F, 1000.0F);
}

void inspectRigidBody(RigidBody& body)
{
    enumCombo("Type", body.type, std::array{"Static", "Dynamic", "Kinematic"});
    ImGui::DragFloat2("Velocity", glm::value_ptr(body.velocity), 0.05F);
    ImGui::DragFloat("Angular velocity", &body.angularVelocity, 0.01F);
    ImGui::DragFloat("Mass", &body.mass, 0.01F, 0.001F, 1e6F);
    ImGui::DragFloat("Gravity scale", &body.gravityScale, 0.01F);
    ImGui::DragFloat("Linear damping", &body.linearDamping, 0.01F, 0.0F, 100.0F);
    ImGui::DragFloat("Angular damping", &body.angularDamping, 0.01F, 0.0F, 100.0F);
    ImGui::Checkbox("Fixed rotation", &body.fixedRotation);
    ImGui::SameLine();
    ImGui::Checkbox("Bullet", &body.bullet);
    ImGui::SameLine();
    ImGui::Checkbox("Allow sleep", &body.allowSleep);
}

void inspectCollider(Collider& collider)
{
    enumCombo("Shape", collider.shape, std::array{"Box", "Circle", "Polygon"});
    switch (collider.shape) {
    case ColliderShape::Box:
        ImGui::DragFloat2("Half extents", glm::value_ptr(collider.halfExtents), 0.01F, 0.001F,
                          1e6F);
        break;
    case ColliderShape::Circle:
        ImGui::DragFloat("Radius", &collider.radius, 0.01F, 0.001F, 1e6F);
        break;
    case ColliderShape::Polygon:
        text("{} corners", collider.points.size());
        break;
    }
    ImGui::DragFloat2("Offset", glm::value_ptr(collider.offset), 0.01F);
    ImGui::SliderFloat("Restitution", &collider.restitution, 0.0F, 1.0F);
    ImGui::DragFloat("Friction", &collider.friction, 0.01F, 0.0F, 10.0F);
    ImGui::Checkbox("Trigger", &collider.isTrigger);
    ImGui::SameLine();
    ImGui::Checkbox("One-way", &collider.oneWay);
    hexInput("Category", collider.category);
    hexInput("Mask", collider.mask);
}

void inspectTilemapCollider(TilemapCollider& tilemap)
{
    const TileGrid& grid = tilemap.grid;
    text("{} x {} tiles of {:.2f}, origin ({:.2f}, {:.2f})", grid.width, grid.height, grid.tileSize,
         grid.origin.x, grid.origin.y);
    ImGui::DragFloat("Friction", &tilemap.friction, 0.01F, 0.0F, 10.0F);
    ImGui::SliderFloat("Restitution", &tilemap.restitution, 0.0F, 1.0F);
    hexInput("Category", tilemap.category);
    hexInput("Mask", tilemap.mask);
}

// Read only: Scene::setParent() keeps the links consistent; the inspector lists the parent.
void inspectRelationship(const Relationship& relationship)
{
    text("{} direct children", relationship.children);
}

void inspectInterpolated(const Interpolated& interpolated)
{
    const Transform& previous = interpolated.previous;
    text("Before the last step: ({:.2f}, {:.2f}){}", previous.position.x, previous.position.y,
         interpolated.valid ? "" : " (not yet valid)");
}

} // namespace

void addBuiltInInspectors(EntityInspector& inspector)
{
    inspector.addInspector<Name>("Name", inspectName);
    inspector.addInspector<Transform>("Transform", inspectTransform);
    inspector.addInspector<SpriteRenderer>("Sprite renderer", inspectSpriteRenderer);
    inspector.addInspector<Animator>("Animator", inspectAnimator);
    inspector.addInspector<Lifetime>("Lifetime", inspectLifetime);
    inspector.addInspector<RigidBody>("Rigid body", inspectRigidBody);
    inspector.addInspector<Collider>("Collider", inspectCollider);
    inspector.addInspector<TilemapCollider>("Tilemap collider", inspectTilemapCollider);
    inspector.addInspector<Relationship>("Relationship", inspectRelationship);
    inspector.addInspector<Interpolated>("Interpolated", inspectInterpolated);
}

} // namespace astra2d::detail
