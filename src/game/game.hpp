#pragma once

#include "assets/game_assets.hpp"
#include "assets/title_assets.hpp"
#include "game/gameplay.hpp"
#include "world/world_map.hpp"

#include "pixel_twins/controller.hpp"
#include "pixel_twins/framebuffer.hpp"
#include "pixel_twins/platform.hpp"
#include "pixel_twins/sprite.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace wizward::game {

enum class Scene : std::uint8_t {
    Title,
    AttractRanking,
    AttractDemo,
    Gameplay,
    Result,
};

enum class AudioEvent : std::uint8_t {
    None,
    PlayField,
    PlayEndless,
    PlayBoss,
    PlayVictory,
    PlayNameEntry,
    StopBgm,
};

struct UpdateResult {
    AudioEvent audio = AudioEvent::None;
    bool playStartSfx = false;
    bool succeeded = true;
    std::array<SfxCue, kMaximumSfxCuesPerTick> sfxCues{};
    std::size_t sfxCueCount = 0;
};

struct CoreLoadSample {
    std::uint32_t renderUs = 0;
    std::uint32_t audioUs = 0;
    std::uint32_t updateUs = 0;
    std::uint32_t otherUs = 0;
};

struct PerformanceOverlay {
    std::array<CoreLoadSample, 2> cores{};
    std::uint16_t fpsTenths = 0;
    bool enabled = false;
};

inline constexpr std::size_t kRankingLimit = 20;

struct RankingRecord {
    std::array<char, 3> name{{'A', 'A', 'A'}};
    std::uint32_t score = 0;
    std::uint32_t timeBonus = 0;
    std::uint8_t player = 0;
    bool cleared = false;
    bool hard = false;
    bool endless = false;
    std::uint32_t survivalTicks = 0;
};

struct RankingEntry {
    std::array<char, 3> name{{'A', 'A', 'A'}};
    std::uint8_t rank = 0;
    std::uint8_t cursor = 0;
    bool active = false;
    bool submitted = false;
};

class Game {
public:
    [[nodiscard]] bool initialize(Scene initialScene = Scene::Title,
                                  std::uint32_t mapSeed = 0x57495aU,
                                  Difficulty difficulty = Difficulty::Easy) noexcept;
    [[nodiscard]] UpdateResult processInput(const pixel_twins::Controllers& controllers) noexcept;
    [[nodiscard]] UpdateResult tick(const pixel_twins::Controllers& controllers) noexcept;
    void render() noexcept PIXEL_TWINS_SRAM;
    void setPerformanceOverlay(const PerformanceOverlay& overlay) noexcept {
        performanceOverlay_ = overlay;
    }
    void setDebugMode(bool enabled) noexcept { debugMode_ = enabled; }

    [[nodiscard]] pixel_twins::Framebuffer& framebuffer() noexcept { return framebuffer_; }
    [[nodiscard]] const pixel_twins::Framebuffer& framebuffer() const noexcept { return framebuffer_; }
    [[nodiscard]] Scene scene() const noexcept { return scene_; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] std::uint32_t mapSeed() const noexcept { return worldMap_.seed; }
    [[nodiscard]] Difficulty difficulty() const noexcept { return difficulty_; }
    [[nodiscard]] bool setDifficulty(Difficulty difficulty) noexcept {
        if (scene_ != Scene::Title) return false;
        difficulty_ = difficulty;
        return true;
    }
    [[nodiscard]] const GameplayState& gameplay() const noexcept { return gameplay_; }
    [[nodiscard]] std::uint32_t timeBonus(std::size_t player) const noexcept {
        return timeBonuses_[player];
    }
    [[nodiscard]] std::uint32_t finalScore(std::size_t player) const noexcept {
        return finalScores_[player];
    }
    [[nodiscard]] const RankingEntry& rankingEntry(std::size_t player) const noexcept {
        return rankingEntries_[player];
    }
    [[nodiscard]] std::size_t rankingCount() const noexcept { return rankingCount_; }
    [[nodiscard]] bool attractMode() const noexcept {
        return scene_ == Scene::AttractRanking || scene_ == Scene::AttractDemo;
    }

private:
    [[nodiscard]] UpdateResult changeScene(Scene scene, bool playStartSfx) noexcept;
    void finalizeResult() noexcept;
    void updateRankingInput(const pixel_twins::Controllers& controllers) noexcept;
    void submitRanking(std::size_t player) noexcept;
    [[nodiscard]] bool hasPendingRanking() const noexcept;

    assets::GameAssets gameAssets_;
    assets::TitleAssets titleAssets_;
    world::TerrainWorkspace terrainWorkspace_;
    world::WorldMap worldMap_;
    pixel_twins::Framebuffer framebuffer_;
    GameplayState gameplay_;
    pixel_twins::SpriteBuckets<
        kMaximumEnemies + kMaximumPlayerBullets + kMaximumXpGems
            + kMaximumEnemyBullets
            + kMaximumWindSlashes * 3U + kMaximumThunderStrikes * 3U
            + kMaximumImpactEffects
            + kMaximumPerkEffects
            + pixel_twins::kControllerCount * 10U
            + pixel_twins::kControllerCount * (4U + kMaximumFamiliarsPerPlayer),
        kMaximumEnemies> spriteBuckets_;
    Scene scene_ = Scene::Title;
    std::uint32_t frame_ = 0;
    std::uint32_t sceneFrame_ = 0;
    std::uint8_t startingPlayer_ = 0;
    std::uint32_t mapSeedState_ = 0x57495aU;
    Difficulty difficulty_ = Difficulty::Easy;
    bool paused_ = false;
    bool nameEntryBgmStarted_ = false;
    bool debugMode_ = false;
    PerformanceOverlay performanceOverlay_{};
    std::array<std::uint32_t, pixel_twins::kControllerCount> timeBonuses_{};
    std::array<std::uint32_t, pixel_twins::kControllerCount> finalScores_{};
    std::array<RankingRecord, kRankingLimit> rankings_{};
    std::array<RankingRecord, kRankingLimit> endlessRankings_{};
    std::array<RankingEntry, pixel_twins::kControllerCount> rankingEntries_{};
    std::size_t rankingCount_ = 0;
    std::size_t endlessRankingCount_ = 0;
    std::array<std::array<char, 3>, pixel_twins::kControllerCount> lastNames_{{
        {{'A', 'A', 'A'}}, {{'A', 'A', 'A'}},
    }};
    std::uint32_t rankingIdleTicks_ = 0;
    std::uint16_t resultContinueTicks_ = 0;
    GameplayOutcome resultOutcome_ = GameplayOutcome::Running;
};

} // namespace wizward::game
