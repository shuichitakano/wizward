#include "ranking_store.hpp"

#include "game/game.hpp"

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/platform.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wizward::rp2350 {
namespace {

extern "C" const std::uint8_t __flash_binary_end;

constexpr std::uint32_t kMagic = 0x4b525a57U; // "WZRK"
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kSlotCount = 2;
constexpr std::size_t kHeaderSize = 20;
constexpr std::size_t kRecordSize = 16;
constexpr std::size_t kRecordCount = game::kRankingLimit * 2U;
constexpr std::size_t kCrcOffset = kHeaderSize + kRecordSize * kRecordCount;
constexpr std::size_t kImageSize = 3U * FLASH_PAGE_SIZE;
static_assert(kCrcOffset + sizeof(std::uint32_t) <= kImageSize);

std::uint16_t read16(const std::uint8_t* source) noexcept {
    return static_cast<std::uint16_t>(
        source[0] | static_cast<std::uint16_t>(source[1]) << 8U);
}

std::uint32_t read32(const std::uint8_t* source) noexcept {
    return static_cast<std::uint32_t>(source[0])
        | static_cast<std::uint32_t>(source[1]) << 8U
        | static_cast<std::uint32_t>(source[2]) << 16U
        | static_cast<std::uint32_t>(source[3]) << 24U;
}

void write16(std::uint8_t* destination, std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8U);
}

void write32(std::uint8_t* destination, std::uint32_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8U);
    destination[2] = static_cast<std::uint8_t>(value >> 16U);
    destination[3] = static_cast<std::uint8_t>(value >> 24U);
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept {
    auto crc = std::uint32_t{0xffffffffU};
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8U; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -static_cast<std::int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

std::uint32_t slotOffset(std::size_t slot) noexcept {
    return PICO_FLASH_SIZE_BYTES
        - static_cast<std::uint32_t>((kSlotCount - slot) * FLASH_SECTOR_SIZE);
}

const std::uint8_t* slotData(std::size_t slot) noexcept {
    return reinterpret_cast<const std::uint8_t*>(
        XIP_BASE + slotOffset(slot));
}

bool validImage(const std::uint8_t* image) noexcept {
    return read32(image) == kMagic
        && read16(image + 4U) == kVersion
        && image[6] <= game::kRankingLimit
        && image[7] <= game::kRankingLimit
        && read32(image + kCrcOffset) == crc32(image, kCrcOffset);
}

bool sequenceIsNewer(std::uint32_t candidate, std::uint32_t current) noexcept {
    return static_cast<std::int32_t>(candidate - current) > 0;
}

game::RankingRecord decodeRecord(const std::uint8_t* source) noexcept {
    game::RankingRecord result{};
    std::copy_n(reinterpret_cast<const char*>(source), result.name.size(),
                result.name.begin());
    const auto flags = source[3];
    result.player = static_cast<std::uint8_t>(flags & 1U);
    result.cleared = (flags & (1U << 1U)) != 0;
    result.hard = (flags & (1U << 2U)) != 0;
    result.endless = (flags & (1U << 3U)) != 0;
    result.score = read32(source + 4U);
    result.timeBonus = read32(source + 8U);
    result.survivalTicks = read32(source + 12U);
    return result;
}

void encodeRecord(std::uint8_t* destination,
                  const game::RankingRecord& record) noexcept {
    std::copy(record.name.begin(), record.name.end(),
              reinterpret_cast<char*>(destination));
    destination[3] = static_cast<std::uint8_t>(
        (record.player & 1U)
        | (record.cleared ? 1U << 1U : 0U)
        | (record.hard ? 1U << 2U : 0U)
        | (record.endless ? 1U << 3U : 0U));
    write32(destination + 4U, record.score);
    write32(destination + 8U, record.timeBonus);
    write32(destination + 12U, record.survivalTicks);
}

struct ProgramContext {
    std::uint32_t offset;
    const std::uint8_t* image;
};

void __not_in_flash_func(programSlot)(void* rawContext) {
    const auto& context = *static_cast<const ProgramContext*>(rawContext);
    flash_range_erase(context.offset, FLASH_SECTOR_SIZE);
    flash_range_program(context.offset, context.image, kImageSize);
}

} // namespace

void RankingStore::load(game::Game& game) noexcept {
    auto selectedSlot = std::int8_t{-1};
    auto selectedSequence = std::uint32_t{0};
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        const auto* image = slotData(slot);
        if (!validImage(image)) continue;
        const auto sequence = read32(image + 8U);
        if (selectedSlot < 0 || sequenceIsNewer(sequence, selectedSequence)) {
            selectedSlot = static_cast<std::int8_t>(slot);
            selectedSequence = sequence;
        }
    }
    if (selectedSlot < 0) return;

    const auto* image = slotData(static_cast<std::size_t>(selectedSlot));
    std::array<game::RankingRecord, game::kRankingLimit> rankings{};
    std::array<game::RankingRecord, game::kRankingLimit> endlessRankings{};
    for (std::size_t index = 0; index < game::kRankingLimit; ++index) {
        rankings[index] = decodeRecord(
            image + kHeaderSize + index * kRecordSize);
        endlessRankings[index] = decodeRecord(
            image + kHeaderSize
                + (game::kRankingLimit + index) * kRecordSize);
    }
    std::array<std::array<char, 3>, pixel_twins::kControllerCount> lastNames{};
    for (std::size_t player = 0; player < lastNames.size(); ++player) {
        std::copy_n(reinterpret_cast<const char*>(image + 12U + player * 3U),
                    3U, lastNames[player].begin());
    }
    game.loadRankings(
        rankings, image[6], endlessRankings, image[7], lastNames);
    activeSlot_ = selectedSlot;
    sequence_ = selectedSequence;
}

bool RankingStore::save(const game::Game& game) noexcept {
    const auto firmwareEnd = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&__flash_binary_end) - XIP_BASE);
    if (firmwareEnd > slotOffset(0)) return false;

    image_.fill(0xffU);
    write32(image_.data(), kMagic);
    write16(image_.data() + 4U, kVersion);
    image_[6] = static_cast<std::uint8_t>(game.rankingCount());
    image_[7] = static_cast<std::uint8_t>(game.endlessRankingCount());
    const auto nextSequence = sequence_ + 1U;
    write32(image_.data() + 8U, nextSequence);
    for (std::size_t player = 0; player < game.lastNames().size(); ++player) {
        std::copy(game.lastNames()[player].begin(), game.lastNames()[player].end(),
                  reinterpret_cast<char*>(image_.data() + 12U + player * 3U));
    }
    for (std::size_t index = 0; index < game::kRankingLimit; ++index) {
        encodeRecord(image_.data() + kHeaderSize + index * kRecordSize,
                     game.rankings()[index]);
        encodeRecord(
            image_.data() + kHeaderSize
                + (game::kRankingLimit + index) * kRecordSize,
            game.endlessRankings()[index]);
    }
    write32(image_.data() + kCrcOffset, crc32(image_.data(), kCrcOffset));

    const auto targetSlot = activeSlot_ == 0 ? std::size_t{1} : std::size_t{0};
    ProgramContext context{slotOffset(targetSlot), image_.data()};
    if (flash_safe_execute(programSlot, &context, 1000U) != PICO_OK) {
        return false;
    }
    if (!validImage(slotData(targetSlot))
        || read32(slotData(targetSlot) + 8U) != nextSequence) {
        return false;
    }
    activeSlot_ = static_cast<std::int8_t>(targetSlot);
    sequence_ = nextSequence;
    return true;
}

} // namespace wizward::rp2350
