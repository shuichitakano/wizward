#include "game/game.hpp"

#include "assets/wizward_font.hpp"

#include "pixel_twins/font.hpp"
#include "pixel_twins/primitives.hpp"
#include "pixel_twins/render_target.hpp"
#include "pixel_twins/sprite.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string_view>

namespace wizward::game {
namespace {

constexpr std::uint32_t kTitleFrames = 150;

std::uint8_t fourDirectionRow(Facing facing) noexcept {
    switch (facing) {
    case Facing::South:
    case Facing::SouthEast:
    case Facing::SouthWest: return 0;
    case Facing::East: return 1;
    case Facing::North:
    case Facing::NorthEast:
    case Facing::NorthWest: return 2;
    case Facing::West: return 3;
    }
    return 0;
}

std::uint32_t perkIconFrame(Perk perk) noexcept {
    return static_cast<std::uint32_t>(perk);
}

PIXEL_TWINS_SRAM void drawPerkChoices(pixel_twins::RenderTarget target,
                     const assets::GameAssets& assets,
                     const PlayerState& player,
                     std::size_t viewer) noexcept {
    constexpr std::array<std::array<std::int16_t, 2>, 4> kCenters{{
        {{118, 92}}, {{132, 80}}, {{146, 92}}, {{132, 104}},
    }};
    constexpr std::array<std::uint8_t, 4> kPackIndices{{1, 0, 2, 3}};
    constexpr std::array<const char*, 4> kP1Labels{{"1", "2", "3", "4"}};
    constexpr std::array<const char*, 4> kP2Labels{{"7", "8", "9", "0"}};
    const auto& labels = viewer == 0 ? kP1Labels : kP2Labels;
    if (player.choosingPerk) {
        for (std::size_t slot = 0; slot < kCenters.size(); ++slot) {
            const auto packIndex = kPackIndices[slot];
            pixel_twins::Sprite sprite{};
            if (assets.makeLoopingSprite(assets::SpriteAssetId::PowerupIcons16Sheet,
                                         perkIconFrame(player.perkChoices[packIndex]), 0,
                                         static_cast<std::int16_t>(kCenters[slot][0] - 8),
                                         static_cast<std::int16_t>(kCenters[slot][1] - 8), sprite)) {
                pixel_twins::drawSprite(target, sprite);
            }
            pixel_twins::drawText(target, assets::kWizwardFont,
                                  static_cast<std::int16_t>(kCenters[slot][0] - 10),
                                  static_cast<std::int16_t>(kCenters[slot][1] - 8),
                                  labels[slot], 255);
        }
    }
    if (player.perkFlashTicks > 0 && (player.perkFlashTicks / 3U) % 2U == 0U) {
        const auto slot = player.perkFlashSlot;
        pixel_twins::Sprite sprite{};
        if (assets.makeLoopingSprite(assets::SpriteAssetId::PowerupIcons16Sheet,
                                     perkIconFrame(player.perkFlash), 0,
                                     static_cast<std::int16_t>(kCenters[slot][0] - 8),
                                     static_cast<std::int16_t>(kCenters[slot][1] - 8), sprite)) {
            pixel_twins::drawSprite(target, sprite);
        }
    }
    for (std::uint8_t index = 1; index < player.pendingPerkChoices; ++index) {
        pixel_twins::fillRectangle(target, static_cast<std::int16_t>(116 + index * 3U), 113, 1, 1, 15);
    }
}

PIXEL_TWINS_SRAM void drawWeaponLevels(pixel_twins::RenderTarget target,
                      const assets::GameAssets& assets,
                      const PlayerState& player) noexcept {
    constexpr std::array<std::uint8_t PlayerState::*, 7> kLevels{{
        &PlayerState::lightLevel, &PlayerState::fireLevel, &PlayerState::windLevel,
        &PlayerState::thunderLevel, &PlayerState::iceLevel, &PlayerState::orbLevel,
        &PlayerState::familiarLevel,
    }};
    std::size_t activeIndex = 0;
    for (std::size_t weapon = 0; weapon < kLevels.size(); ++weapon) {
        const auto level = player.*kLevels[weapon];
        if (level == 0) continue;
        const auto x = static_cast<std::int16_t>(4 + (activeIndex % 4U) * 18U);
        const auto y = static_cast<std::int16_t>(101 + (activeIndex / 4U) * 10U);
        pixel_twins::Sprite icon{};
        if (assets.makeLoopingSprite(assets::SpriteAssetId::PowerupIcons8Sheet,
                                     static_cast<std::uint32_t>(weapon), 0, x, y, icon)) {
            pixel_twins::drawSprite(target, icon);
        }
        char levelText[3]{};
        const auto digits = level >= 10 ? 2U : 1U;
        if (digits == 2U) levelText[0] = static_cast<char>('0' + level / 10U);
        levelText[digits - 1U] = static_cast<char>('0' + level % 10U);
        pixel_twins::drawText(target, assets::kWizwardFont,
                              static_cast<std::int16_t>(x + 10), y,
                              std::string_view(levelText, digits), 255);
        ++activeIndex;
    }
}

std::string_view formatUnsigned(std::uint32_t value, char (&buffer)[12]) noexcept {
    std::size_t digits = 1;
    for (auto remaining = value; remaining >= 10U; remaining /= 10U) ++digits;
    auto remaining = value;
    for (std::size_t index = digits; index > 0; --index) {
        buffer[index - 1U] = static_cast<char>('0' + remaining % 10U);
        remaining /= 10U;
    }
    return {buffer, digits};
}

PIXEL_TWINS_SRAM void drawRightAlignedText(pixel_twins::RenderTarget target,
                          std::string_view text,
                          std::int16_t right,
                          std::int16_t y,
                          std::int16_t stride = 6) noexcept {
    const auto width = static_cast<std::int16_t>(
        text.empty() ? 0 : (text.size() - 1U) * static_cast<std::size_t>(stride) + 8U);
    pixel_twins::drawText(target, assets::kWizwardFont,
                          static_cast<std::int16_t>(right - width), y, text, 18, stride);
}

PIXEL_TWINS_SRAM void drawCenteredText(pixel_twins::RenderTarget target,
                      std::string_view text,
                      std::int16_t center,
                      std::int16_t y,
                      std::int16_t stride = 6) noexcept {
    const auto width = static_cast<std::int16_t>(
        text.empty() ? 0 : (text.size() - 1U) * static_cast<std::size_t>(stride) + 8U);
    pixel_twins::drawText(target, assets::kWizwardFont,
                          static_cast<std::int16_t>(center - width / 2), y, text, 18, stride);
}

PIXEL_TWINS_SRAM void drawTimer(pixel_twins::RenderTarget target,
               std::uint32_t elapsedTicks) noexcept {
    constexpr std::uint32_t kGameSeconds = 300;
    const auto elapsedSeconds = elapsedTicks / 60U;
    const auto remain = elapsedSeconds < kGameSeconds ? kGameSeconds - elapsedSeconds : 0U;
    const auto elapsedMinute = elapsedSeconds / 60U;
    const auto sinceMinute = elapsedSeconds - elapsedMinute * 60U;
    if ((elapsedSeconds < 4U || (elapsedMinute > 0U && elapsedMinute < 5U && sinceMinute < 4U))
        && (elapsedTicks / 15U) % 2U == 0U) {
        char text[] = "5 MIN LEFT";
        text[0] = static_cast<char>('0' + (remain + 59U) / 60U);
        drawCenteredText(target, text, 80, 24);
    } else if (remain <= 60U && elapsedSeconds >= 4U) {
        char text[] = "0:00";
        text[2] = static_cast<char>('0' + (remain / 10U) % 10U);
        text[3] = static_cast<char>('0' + remain % 10U);
        drawCenteredText(target, text, 80, 24);
    }
}

PIXEL_TWINS_SRAM void drawMiniMap(pixel_twins::RenderTarget target,
                 const world::WorldMap& map,
                 const GameplayState& gameplay,
                 std::size_t viewer) noexcept {
    constexpr std::int16_t kX = 5;
    constexpr std::int16_t kY = 5;
    constexpr std::uint16_t kSize = 32;
    pixel_twins::fillRectangle(target, kX, kY, kSize, kSize, 0);
    for (std::uint16_t my = 0; my < kSize; ++my) {
        for (std::uint16_t mx = 0; mx < kSize; ++mx) {
            const auto tx = static_cast<std::uint16_t>(mx * world::kMapColumns / kSize);
            const auto ty = static_cast<std::uint16_t>(my * world::kMapRows / kSize);
            pixel_twins::fillRectangle(target,
                static_cast<std::int16_t>(kX + mx), static_cast<std::int16_t>(kY + my), 1, 1,
                map.collides(tx, ty) ? 29 : 30);
        }
    }
    pixel_twins::drawRectangle(target, kX, kY, kSize, kSize, 32);
    for (const auto& seal : map.seals) {
        const auto x = static_cast<std::int16_t>(kX + seal.x * kSize / world::kMapColumns);
        const auto y = static_cast<std::int16_t>(kY + seal.y * kSize / world::kMapRows);
        pixel_twins::fillCircle(target, x, y, 1, 215);
    }
    for (std::size_t index = 0; index < pixel_twins::kControllerCount; ++index) {
        const auto& player = gameplay.player(index);
        const auto x = static_cast<std::int16_t>(kX + player.x * kSize
            / static_cast<float>(world::kMapColumns * kWorldTileSize));
        const auto y = static_cast<std::int16_t>(kY + player.y * kSize
            / static_cast<float>(world::kMapRows * kWorldTileSize));
        pixel_twins::fillCircle(target, x, y, index == viewer ? 2 : 1, index == 0 ? 19 : 20);
    }
}

PIXEL_TWINS_SRAM void drawOffscreenPartnerArrow(pixel_twins::RenderTarget target,
                               const GameplayState& gameplay,
                               const CameraState& camera,
                               std::size_t viewer) noexcept {
    const auto& player = gameplay.player(viewer);
    const auto& partner = gameplay.player(1U - viewer);
    const auto screenX = partner.x - camera.x;
    const auto screenY = partner.y - camera.y;
    if (screenX >= 0.0F && screenX <= 160.0F && screenY >= 0.0F && screenY <= 120.0F) return;
    auto dx = partner.x - player.x;
    auto dy = partner.y - player.y;
    const auto length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0F) return;
    dx /= length;
    dy /= length;
    const auto x = std::clamp(80.0F + dx * 70.0F, 8.0F, 152.0F);
    const auto y = std::clamp(60.0F + dy * 52.0F, 14.0F, 112.0F);
    pixel_twins::fillTriangle(target,
        static_cast<std::int16_t>(std::round(x + dx * 5.0F)),
        static_cast<std::int16_t>(std::round(y + dy * 5.0F)),
        static_cast<std::int16_t>(std::round(x - dy * 4.0F - dx * 3.0F)),
        static_cast<std::int16_t>(std::round(y + dx * 4.0F - dy * 3.0F)),
        static_cast<std::int16_t>(std::round(x + dy * 4.0F - dx * 3.0F)),
        static_cast<std::int16_t>(std::round(y - dx * 4.0F - dy * 3.0F)),
        viewer == 0 ? 20 : 19);
}

template<std::size_t Capacity>
PIXEL_TWINS_SRAM void queueAsset(pixel_twins::SpriteBuckets<Capacity, 0>& buckets,
                const assets::GameAssets& assets,
                assets::SpriteAssetId id,
                std::uint32_t frame,
                std::int16_t x,
                std::int16_t y,
                float screenFootY,
                std::uint8_t directionRow = 0) noexcept {
    pixel_twins::Sprite sprite{};
    if (!assets.makeLoopingSprite(id, frame, directionRow, x, y, sprite)) return;
    constexpr float kSortMargin = 60.0F;
    const auto bucket = static_cast<std::uint16_t>(std::clamp(
        static_cast<std::int32_t>(screenFootY + kSortMargin), 0,
        static_cast<std::int32_t>(pixel_twins::kBucketCount - 1U)));
    (void)buckets.addSprite(bucket, sprite);
}

PIXEL_TWINS_SRAM void drawTitle(pixel_twins::Framebuffer& framebuffer,
               const assets::TitleAssets& title,
               std::uint32_t frame) noexcept {
    auto left = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Left);
    auto right = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Right);
    title.drawScreen(left);
    title.drawScreen(right);
    pixel_twins::Sprite logo{};
    if (title.makeLogo(28, 18, logo)) pixel_twins::drawSprite(right, logo);
    if ((frame / 30U) % 2U == 0U) {
        pixel_twins::drawText(left, assets::kWizwardFont, 40, 98, "PRESS START", 255);
        pixel_twins::drawText(right, assets::kWizwardFont, 40, 98, "PRESS START", 255);
    }
}

template<std::size_t Capacity>
PIXEL_TWINS_SRAM void drawGameplayPanel(pixel_twins::RenderTarget target,
                       const world::WorldMap& map,
                       const assets::GameAssets& assets,
                       const CameraState& camera,
                       const GameplayState& gameplay,
                       pixel_twins::SpriteBuckets<Capacity, 0>& spriteBuckets,
                       std::uint32_t frame,
                       std::size_t viewer) noexcept {
    map.draw(target, assets.background(), static_cast<std::int32_t>(camera.x),
             static_cast<std::int32_t>(camera.y));
    spriteBuckets.reset();
    for (const auto& bullet : gameplay.bullets()) {
        if (!bullet.active) continue;
        auto asset = assets::SpriteAssetId::LightOrb88x83fSheet;
        std::int16_t halfSize = 4;
        std::uint8_t directionRow = 0;
        if (bullet.type == PlayerAttack::Fire) {
            asset = bullet.damage >= 19 ? assets::SpriteAssetId::Fireball1616x163fSheet
                                        : assets::SpriteAssetId::Fireball1212x123fSheet;
            halfSize = bullet.damage >= 19 ? 8 : 6;
        } else if (bullet.type == PlayerAttack::Ice) {
            asset = assets::SpriteAssetId::IceShard128dir12x128fSheet;
            halfSize = 6;
            directionRow = directionRow8(bullet.velocityX, bullet.velocityY);
        }
        queueAsset(spriteBuckets, assets, asset, frame / 6U,
                   static_cast<std::int16_t>(bullet.x - camera.x - halfSize),
                   static_cast<std::int16_t>(bullet.y - camera.y - 10.0F - halfSize),
                   bullet.y - camera.y, directionRow);
    }
    for (const auto& gem : gameplay.xpGems()) {
        if (!gem.active) continue;
        queueAsset(spriteBuckets, assets, assets::SpriteAssetId::XpGem88x81fSheet, 0,
                   static_cast<std::int16_t>(gem.x - camera.x - 4.0F),
                   static_cast<std::int16_t>(gem.y - camera.y - 4.0F), gem.y - camera.y);
    }
    for (const auto& enemy : gameplay.enemies()) {
        if (!enemy.active) continue;
        queueAsset(spriteBuckets, assets, assets::SpriteAssetId::SlimeEnemyWalk16x164fSmallerSheet,
                   frame / 8U,
                   static_cast<std::int16_t>(enemy.x - camera.x - 8.0F),
                   static_cast<std::int16_t>(enemy.y - camera.y - 12.0F), enemy.y - camera.y,
                   fourDirectionRow(enemy.facing));
    }
    for (const auto& slash : gameplay.windSlashes()) {
        if (!slash.active || slash.owner >= pixel_twins::kControllerCount) continue;
        const auto& owner = gameplay.player(slash.owner);
        const auto sweep = static_cast<float>(slash.ageTicks) / 28.0F * 2.35F * 3.1415927F;
        const auto radius = (slash.innerRadius + slash.outerRadius) * 0.5F;
        for (std::uint8_t blade = 0; blade < slash.bladeCount; ++blade) {
            const auto angle = slash.startAngle + static_cast<float>(blade) * 6.2831853F
                / slash.bladeCount + sweep;
            const auto x = owner.x + std::cos(angle) * radius;
            const auto y = owner.y + std::sin(angle) * radius;
            const auto animationFrame = static_cast<std::uint32_t>(7U - std::min<std::uint16_t>(7U, slash.ageTicks / 2U));
            queueAsset(spriteBuckets, assets, assets::SpriteAssetId::WindSlash1616x168fSheet,
                       animationFrame,
                       static_cast<std::int16_t>(x - camera.x - 8.0F),
                       static_cast<std::int16_t>(y - camera.y - 18.0F), y - camera.y);
        }
    }
    constexpr std::array<std::array<std::int16_t, 4>, 3> kThunderConnections{{
        {{14, 0, 10, 59}}, {{10, 0, 12, 59}}, {{12, 0, 13, 59}},
    }};
    for (const auto& strike : gameplay.thunderStrikes()) {
        if (!strike.active) continue;
        auto connectX = strike.x - camera.x;
        auto connectY = strike.y - camera.y;
        for (std::uint8_t segment = 0; segment < 2; ++segment) {
            const auto animationFrame = static_cast<std::uint8_t>((strike.ageTicks / 3U + segment) % 3U);
            const auto& anchors = kThunderConnections[animationFrame];
            const auto drawX = static_cast<std::int16_t>(std::round(connectX - anchors[2]));
            const auto drawY = static_cast<std::int16_t>(std::round(connectY - anchors[3]));
            queueAsset(spriteBuckets, assets, assets::SpriteAssetId::ThunderPillar24x603fSheet,
                       animationFrame, drawX, drawY, strike.y - camera.y);
            connectX = static_cast<float>(drawX + anchors[0]);
            connectY = static_cast<float>(drawY + anchors[1]);
        }
        constexpr std::uint16_t kThunderImpactTicks = 10;
        if (strike.ageTicks < kThunderImpactTicks) {
            const auto impactFrame = static_cast<std::uint32_t>(std::min<std::uint16_t>(
                2U, static_cast<std::uint16_t>(strike.ageTicks * 3U / kThunderImpactTicks)));
            queueAsset(spriteBuckets, assets, assets::SpriteAssetId::ThunderImpact1616x163fSheet,
                       impactFrame,
                       static_cast<std::int16_t>(std::round(strike.x - camera.x - 8.0F)),
                       static_cast<std::int16_t>(std::round(strike.y - camera.y - 8.0F)),
                       strike.y - camera.y);
        }
    }
    for (std::size_t playerIndex = 0; playerIndex < pixel_twins::kControllerCount; ++playerIndex) {
        const auto& player = gameplay.player(playerIndex);
        if (player.orbLevel == 0) continue;
        const auto points = static_cast<unsigned>(player.orbLevel - 1U);
        const auto count = static_cast<std::uint8_t>(1U + (points + 2U) / 3U);
        const auto radius = 22.0F + static_cast<float>(points / 3U) * 4.0F;
        for (std::uint8_t orb = 0; orb < count; ++orb) {
            const auto angle = player.orbAngle + static_cast<float>(orb) * 6.2831853F / count;
            const auto x = player.x + std::cos(angle) * radius;
            const auto y = player.y + std::sin(angle) * radius;
            queueAsset(spriteBuckets, assets, assets::SpriteAssetId::OrbSatellite88x83fSheet,
                       frame / 7U,
                       static_cast<std::int16_t>(x - camera.x - 4.0F),
                       static_cast<std::int16_t>(y - camera.y - 14.0F), y - camera.y);
        }
    }
    for (std::size_t index = 0; index < pixel_twins::kControllerCount; ++index) {
        const auto& player = gameplay.player(index);
        if (player.invulnerabilityTicks > 0 && (player.invulnerabilityTicks / 3U) % 2U != 0U) continue;
        const auto animationFrame = frame / (player.moving ? 7U : 10U);
        const auto anchorX = index == 0 ? 12.0F : 14.0F;
        constexpr float kPlayerAnchorY = 30.0F;
        const auto screenX = static_cast<std::int16_t>(player.x - camera.x - anchorX);
        const auto screenY = static_cast<std::int16_t>(player.y - camera.y - kPlayerAnchorY);
        const auto directionRow = static_cast<std::uint8_t>(player.facing);
        const auto asset = index == 0
            ? (player.moving ? assets::SpriteAssetId::GirlMageWalk24x324fSheet
                             : assets::SpriteAssetId::GirlMageIdle24x324fSheet)
            : (player.moving ? assets::SpriteAssetId::BoyMageWalkStaff28x326fSheet
                             : assets::SpriteAssetId::BoyMageIdleStaff28x324fSheet);
        queueAsset(spriteBuckets, assets, asset, animationFrame, screenX, screenY,
                   player.y - camera.y, directionRow);
    }
    spriteBuckets.draw(target);
    for (const auto& strike : gameplay.thunderStrikes()) {
        if (!strike.active) continue;
        pixel_twins::drawCircle(target,
            static_cast<std::int16_t>(std::round(strike.x - camera.x)),
            static_cast<std::int16_t>(std::round(strike.y - camera.y)),
            thunderShockwaveRadius(strike.ageTicks), 4);
    }
    const auto& viewedPlayer = gameplay.player(viewer);
    const auto maxHpWidth = static_cast<std::uint16_t>(std::clamp<std::int16_t>(viewedPlayer.maxHp, 1, 60));
    const auto hpWidth = static_cast<std::uint16_t>(
        std::clamp<std::int16_t>(viewedPlayer.hp, 0, static_cast<std::int16_t>(maxHpWidth)));
    pixel_twins::fillRectangle(target, 49, 4, static_cast<std::uint16_t>(maxHpWidth + 2U), 4, 28);
    if (hpWidth > 0) pixel_twins::fillRectangle(target, 50, 5, hpWidth, 2, 9);
    pixel_twins::fillRectangle(target, 49, 7, 62, 4, 28);
    const auto xpNeed = xpNeededForLevel(viewedPlayer.level);
    const auto xpWidth = static_cast<std::uint16_t>(viewedPlayer.xp * 60U / xpNeed);
    if (xpWidth > 0) pixel_twins::fillRectangle(target, 50, 8, xpWidth, 2, 20);
    char scoreBuffer[12]{};
    drawRightAlignedText(target, formatUnsigned(gameplay.score(viewer), scoreBuffer), 155, 5);
    drawTimer(target, gameplay.elapsedTicks());
    drawWeaponLevels(target, assets, viewedPlayer);
    drawPerkChoices(target, assets, viewedPlayer, viewer);
    drawMiniMap(target, map, gameplay, viewer);
    drawOffscreenPartnerArrow(target, gameplay, camera, viewer);
}

} // namespace

bool Game::initialize(Scene initialScene) noexcept {
    world::MapGenerator mapGenerator;
    if (!gameAssets_.initialize() || !titleAssets_.initialize()
        || !mapGenerator.generate(0x57495aU, gameAssets_.background(), terrainWorkspace_, worldMap_)) {
        return false;
    }
    gameplay_.reset(worldMap_);
    scene_ = initialScene;
    return scene_ == Scene::Title ? titleAssets_.applyPalette(framebuffer_)
                                  : gameAssets_.applyPalette(framebuffer_);
}

UpdateResult Game::changeScene(Scene scene, bool playStartSfx) noexcept {
    scene_ = scene;
    sceneFrame_ = 0;
    if (scene_ == Scene::Gameplay) gameplay_.reset(worldMap_);
    const bool paletteApplied = scene_ == Scene::Title
        ? titleAssets_.applyPalette(framebuffer_)
        : gameAssets_.applyPalette(framebuffer_);
    AudioEvent audio = AudioEvent::StopBgm;
    if (scene_ == Scene::Gameplay) audio = AudioEvent::PlayField;
    if (scene_ == Scene::Result) audio = AudioEvent::PlayVictory;
    return {audio, playStartSfx, paletteApplied};
}

UpdateResult Game::processInput(const pixel_twins::Controllers& controllers) noexcept {
    if (!controllers[0].isPressed(pixel_twins::ControllerButton::start)) return {};
    const auto next = scene_ == Scene::Title ? Scene::Gameplay
        : (scene_ == Scene::Gameplay ? Scene::Result : Scene::Title);
    return changeScene(next, true);
}

UpdateResult Game::tick(const pixel_twins::Controllers& controllers) noexcept {
    if (scene_ == Scene::Title && sceneFrame_ == kTitleFrames) {
        auto result = changeScene(Scene::Gameplay, false);
        ++frame_;
        ++sceneFrame_;
        return result;
    }
    if (scene_ == Scene::Gameplay) {
        gameplay_.tick(controllers, worldMap_);
    }
    ++frame_;
    ++sceneFrame_;
    return {};
}

void Game::render() noexcept {
    if (scene_ == Scene::Title) {
        drawTitle(framebuffer_, titleAssets_, frame_);
    } else if (scene_ == Scene::Gameplay) {
        const auto left = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Left);
        const auto right = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Right);
        drawGameplayPanel(left, worldMap_, gameAssets_, gameplay_.camera(0), gameplay_,
                          spriteBuckets_, frame_, 0);
        drawGameplayPanel(right, worldMap_, gameAssets_, gameplay_.camera(1), gameplay_,
                          spriteBuckets_, frame_, 1);
    } else {
        const auto left = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Left);
        const auto right = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Right);
        pixel_twins::fillRectangle(left, 0, 0, 160, 120, 0);
        pixel_twins::fillRectangle(right, 0, 0, 160, 120, 0);
        pixel_twins::drawText(left, assets::kWizwardFont, 53, 48, "RESULT", 255);
        pixel_twins::drawText(right, assets::kWizwardFont, 53, 48, "RESULT", 255);
    }
    framebuffer_.flip();
}

} // namespace wizward::game
