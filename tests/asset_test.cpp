#include "assets/game_assets.hpp"
#include "assets/title_assets.hpp"
#include "audio/sfx_data.hpp"

#include "pixel_twins/framebuffer.hpp"
#include "pixel_twins/render_target.hpp"

#include <cassert>

int main() {
    wizward::assets::GameAssets assets;
    assert(assets.initialize());
    assert(assets.valid());
    assert(assets.background().patternCount() == 123);
    assert(assets.sprites().assetCount() == wizward::assets::kSpriteAssetCount);

    pixel_twins::Framebuffer framebuffer;
    assert(assets.applyPalette(framebuffer));
    assert(framebuffer.palette()[0].r == 0);
    assert(framebuffer.palette()[255].r == 255);

    pixel_twins::Sprite sprite{};
    assert(assets.makeSprite(
        wizward::assets::SpriteAssetId::BossBlueFireball1616x163fSheet,
        0,
        0,
        20,
        30,
        sprite));
    assert(sprite.p != nullptr && sprite.sw > 0 && sprite.sh > 0);
    assert(assets.animationFrameCount(
        wizward::assets::SpriteAssetId::BossBlueFireball1616x163fSheet) == 3);
    assert(assets.animationFrameCount(
        wizward::assets::SpriteAssetId::PlazaRecallCircle32) == 1);
    assert(assets.animationFrameCount(
        wizward::assets::SpriteAssetId::BombCoreWave64x487fSheet) == 7);
    assert(assets.animationFrameCount(
        wizward::assets::SpriteAssetId::BombRay16x161f8dirSheet) == 1);
    assert(assets.makeLoopingSprite(
        wizward::assets::SpriteAssetId::BossBlueFireball1616x163fSheet,
        4,
        0,
        20,
        30,
        sprite));
    pixel_twins::Sprite p1Gem{};
    pixel_twins::Sprite p2Gem{};
    assert(assets.makeXpGemSprite(0, 0, 0, p1Gem));
    assert(assets.makeXpGemSprite(1, 0, 0, p2Gem));
    assert(p1Gem.sw == 8 && p1Gem.sh == 8);
    bool gemColorsDiffer = false;
    for (std::size_t pixel = 0; pixel < 64; ++pixel) {
        assert((p1Gem.p[pixel] == pixel_twins::kTransparentColor)
               == (p2Gem.p[pixel] == pixel_twins::kTransparentColor));
        gemColorsDiffer = gemColorsDiffer || p1Gem.p[pixel] != p2Gem.p[pixel];
    }
    assert(gemColorsDiffer);

    wizward::assets::TitleAssets title;
    assert(title.initialize());
    assert(title.applyPalette(framebuffer));
    auto target = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Left);
    title.drawScreen(target);
    assert(framebuffer.drawBuffer()[0] == wizward::assets::kTitleScreenData[0]);
    assert(title.makeLogo(28, 12, sprite));
    assert(sprite.p != nullptr && sprite.sw <= 104 && sprite.sh <= 20);
    assert(wizward::audio::kLightCast.timbre.wave != nullptr);
    assert(wizward::audio::kBossDeathBlast.priority > wizward::audio::kHit.priority);
    return 0;
}
