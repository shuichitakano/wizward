#include "assets/game_assets.hpp"
#include "assets/title_assets.hpp"

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
    assert(assets.makeLoopingSprite(
        wizward::assets::SpriteAssetId::BossBlueFireball1616x163fSheet,
        4,
        0,
        20,
        30,
        sprite));

    wizward::assets::TitleAssets title;
    assert(title.initialize());
    assert(title.applyPalette(framebuffer));
    auto target = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Left);
    title.drawScreen(target);
    assert(framebuffer.drawBuffer()[0] == wizward::assets::kTitleScreenData[0]);
    assert(title.makeLogo(28, 12, sprite));
    assert(sprite.p != nullptr && sprite.sw <= 104 && sprite.sh <= 20);
    return 0;
}
