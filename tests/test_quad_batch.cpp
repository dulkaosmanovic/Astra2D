// Tests the CPU side of Renderer2D's batching (src/renderer/quad_batch.hpp), no OpenGL needed.
#include "renderer/quad_batch.hpp"

#include <gtest/gtest.h>

#include <numbers>

using astra2d::TextureRegion;
using astra2d::detail::QuadBatch;
using astra2d::detail::QuadCorners;

namespace {

constexpr std::uint32_t kWhite = 1;

void expectNear(glm::vec2 actual, glm::vec2 expected)
{
    EXPECT_NEAR(actual.x, expected.x, 1e-5F);
    EXPECT_NEAR(actual.y, expected.y, 1e-5F);
}

QuadCorners unitQuad()
{
    return astra2d::detail::rectangleCorners({0.0F, 0.0F}, {1.0F, 1.0F}, 0.0F);
}

} // namespace

TEST(QuadGeometryTests, RectangleCornersAreCounterClockwiseFromBottomLeft)
{
    const QuadCorners corners = astra2d::detail::rectangleCorners({10.0F, 20.0F}, {4.0F, 2.0F}, 0);
    expectNear(corners[0], {8.0F, 19.0F});
    expectNear(corners[1], {12.0F, 19.0F});
    expectNear(corners[2], {12.0F, 21.0F});
    expectNear(corners[3], {8.0F, 21.0F});
}

TEST(QuadGeometryTests, RotationIsCounterClockwiseAroundTheCentre)
{
    const float quarterTurn = std::numbers::pi_v<float> / 2.0F;
    const QuadCorners corners =
        astra2d::detail::rectangleCorners({0.0F, 0.0F}, {2.0F, 2.0F}, quarterTurn);
    expectNear(corners[0], {1.0F, -1.0F}); // bottom-left moved to bottom-right
    expectNear(corners[2], {-1.0F, 1.0F});
}

TEST(QuadGeometryTests, LineIsARectangleAlongTheSegment)
{
    QuadCorners corners{};
    ASSERT_TRUE(astra2d::detail::lineCorners({0.0F, 0.0F}, {10.0F, 0.0F}, 2.0F, corners));
    expectNear(corners[0], {0.0F, -1.0F});
    expectNear(corners[1], {10.0F, -1.0F});
    expectNear(corners[2], {10.0F, 1.0F});
    expectNear(corners[3], {0.0F, 1.0F});
    EXPECT_FALSE(astra2d::detail::lineCorners({3.0F, 3.0F}, {3.0F, 3.0F}, 2.0F, corners));
}

TEST(QuadGeometryTests, IndicesFormTwoTrianglesPerQuad)
{
    const auto indices = astra2d::detail::quadIndices(2);
    const std::vector<std::uint32_t> expected{0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};
    EXPECT_EQ(indices, expected);
}

TEST(QuadBatchTests, UntexturedQuadsUseTheWhiteTextureInSlotZero)
{
    QuadBatch batch(10, 4, kWhite);
    ASSERT_TRUE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 0));
    ASSERT_EQ(batch.textures().size(), 1U);
    EXPECT_EQ(batch.textures()[0], kWhite);
    for (const auto& vertex : batch.vertices()) {
        EXPECT_EQ(vertex.textureSlot, 0);
    }
}

TEST(QuadBatchTests, VerticesGetCornersColorAndUvs)
{
    QuadBatch batch(10, 4, kWhite);
    const glm::vec4 red{1.0F, 0.0F, 0.0F, 1.0F};
    const TextureRegion region{.uvMin = {0.25F, 0.5F}, .uvMax = {0.5F, 1.0F}};
    ASSERT_TRUE(batch.add(unitQuad(), red, region, 7));
    const auto vertices = batch.vertices();
    ASSERT_EQ(vertices.size(), 4U);
    expectNear(vertices[0].uv, {0.25F, 0.5F});
    expectNear(vertices[1].uv, {0.5F, 0.5F});
    expectNear(vertices[2].uv, {0.5F, 1.0F});
    expectNear(vertices[3].uv, {0.25F, 1.0F});
    EXPECT_EQ(vertices[2].color, red);
    EXPECT_EQ(vertices[0].textureSlot, 1);
}

TEST(QuadBatchTests, SameTextureSharesASlot)
{
    QuadBatch batch(10, 4, kWhite);
    ASSERT_TRUE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 7));
    ASSERT_TRUE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 8));
    ASSERT_TRUE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 7));
    EXPECT_EQ(batch.textures().size(), 3U); // white, 7, 8
    EXPECT_EQ(batch.vertices()[8].textureSlot, 1);
}

TEST(QuadBatchTests, RefusesQuadWhenFull)
{
    QuadBatch batch(2, 4, kWhite);
    EXPECT_TRUE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 0));
    EXPECT_TRUE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 0));
    EXPECT_FALSE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 0));
    EXPECT_EQ(batch.quadCount(), 2U);
}

TEST(QuadBatchTests, RefusesNewTextureWhenSlotsAreTaken)
{
    QuadBatch batch(100, 3, kWhite); // white + 2 free slots
    EXPECT_TRUE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 10));
    EXPECT_TRUE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 11));
    EXPECT_FALSE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 12));
    // Textures already in the batch and untextured quads still fit.
    EXPECT_TRUE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 10));
    EXPECT_TRUE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 0));
    EXPECT_EQ(batch.quadCount(), 4U);
}

TEST(QuadBatchTests, ClearKeepsWhiteTexture)
{
    QuadBatch batch(10, 4, kWhite);
    ASSERT_TRUE(batch.add(unitQuad(), glm::vec4(1.0F), TextureRegion{}, 7));
    batch.clear();
    EXPECT_TRUE(batch.empty());
    ASSERT_EQ(batch.textures().size(), 1U);
    EXPECT_EQ(batch.textures()[0], kWhite);
}
