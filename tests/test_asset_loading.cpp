// Error paths of file loading. They fail before any OpenGL call, so no context is needed.
#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/renderer/shader.hpp>
#include <astra2d/renderer/texture.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

using astra2d::AssetCache;
using astra2d::Shader;
using astra2d::Texture2D;

TEST(AssetLoadingTests, MissingShaderFileThrowsWithItsName)
{
    try {
        (void)Shader::fromFiles("no/such/shader.vert", "no/such/shader.frag");
        FAIL() << "expected an exception";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("no/such/shader.vert"), std::string::npos)
            << error.what();
    }
}

TEST(AssetLoadingTests, MissingTextureFileThrowsWithItsName)
{
    try {
        (void)Texture2D::fromFile("no/such/texture.png");
        FAIL() << "expected an exception";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("texture.png"), std::string::npos) << error.what();
    }
}

TEST(AssetLoadingTests, TexturePixelDataMustMatchItsSize)
{
    const std::array<std::uint8_t, 4> onePixel{255, 255, 255, 255};
    EXPECT_THROW(Texture2D(2, 2, onePixel), std::invalid_argument);
    EXPECT_THROW(Texture2D(0, 1, onePixel), std::invalid_argument);
}

TEST(AssetLoadingTests, FailedLoadIsNotCached)
{
    AssetCache assets("no/such/root");
    EXPECT_THROW((void)assets.texture("missing.png"), std::runtime_error);
    EXPECT_EQ(assets.textureCount(), 0U);
}

TEST(AssetLoadingTests, ResolveJoinsRootAndNormalizes)
{
    // The root is canonicalized like the paths, so the test also holds where the temporary
    // directory is a symlink (e.g. /tmp -> /private/tmp on macOS).
    const std::filesystem::path root =
        std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) /
        "astra2d-assets";
    const AssetCache assets(root);
    EXPECT_EQ(assets.resolve("textures/../textures/a.png"), root / "textures" / "a.png");
    EXPECT_EQ(assets.resolve("./a.png"), root / "a.png");
}

TEST(AssetLoadingTests, AbsolutePathsIgnoreTheRoot)
{
    const AssetCache assets("some/root");
    const std::filesystem::path absolute =
        std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) / "b.png";
    EXPECT_EQ(assets.resolve(absolute), absolute);
}
