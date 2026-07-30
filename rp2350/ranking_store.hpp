#pragma once

#include <array>
#include <cstdint>

namespace wizward::game {
class Game;
}

namespace wizward::rp2350 {

class RankingStore {
public:
    void load(game::Game& game) noexcept;
    [[nodiscard]] bool save(const game::Game& game) noexcept;

private:
    static constexpr std::size_t kImageSize = 3U * 256U;

    std::array<std::uint8_t, kImageSize> image_{};
    std::uint32_t sequence_ = 0;
    std::int8_t activeSlot_ = -1;
};

} // namespace wizward::rp2350
