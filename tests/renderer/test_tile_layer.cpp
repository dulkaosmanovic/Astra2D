#include <astra2d/renderer/tile_layer.hpp>

#include <gtest/gtest.h>

using astra2d::TileLayer;

TEST(TileLayerTests, CellsStartEmptyAndSetFillsTheLayer)
{
    TileLayer layer{.width = 3, .height = 2, .tileSize = 16.0F};
    EXPECT_EQ(layer.at(1, 1), TileLayer::kEmpty); // no frames yet
    layer.set(2, 1, 7);
    EXPECT_EQ(layer.frames.size(), 6U);
    EXPECT_EQ(layer.at(2, 1), 7);
    EXPECT_EQ(layer.at(0, 0), TileLayer::kEmpty);
    EXPECT_EQ(layer.at(3, 1), TileLayer::kEmpty); // outside
    EXPECT_EQ(layer.at(-1, 0), TileLayer::kEmpty);
}

TEST(TileLayerTests, CellCentresStartAtTheOrigin)
{
    const TileLayer layer{.width = 2, .height = 2, .tileSize = 16.0F, .origin = {100.0F, -32.0F}};
    EXPECT_EQ(layer.cellCenter(0, 0), glm::vec2(108.0F, -24.0F));
    EXPECT_EQ(layer.cellCenter(1, 1), glm::vec2(124.0F, -8.0F));
}
