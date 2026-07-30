#pragma once

#include "pico/critical_section.h"
#include "pico/platform.h"
#include "pico/time.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace wizward::rp2350 {

class JobCounter {
public:
    [[nodiscard]] bool complete() const noexcept {
        return remaining_.load(std::memory_order_acquire) == 0;
    }

private:
    friend class JobSystem;
    std::atomic<std::uint32_t> remaining_{0};
};

class JobSystem {
public:
    enum class Category : std::uint8_t {
        Render,
        Audio,
        Game,
        Other,
        Count,
    };

    struct Profile {
        std::uint32_t renderUs = 0;
        std::uint32_t audioUs = 0;
        std::uint32_t gameUs = 0;
        std::uint32_t otherUs = 0;
    };

    // contextの寿命は完了カウンタが0になるまで呼び出し側が保証する。
    // LEDのフレーム境界を遅延させない、短時間かつ非ブロッキングな処理だけを投入する。
    using Function = void (*)(void*) noexcept;
    using InterruptTimeProvider = std::uint32_t (*)() noexcept;

    void initialize(InterruptTimeProvider interruptTimeProvider = nullptr) noexcept {
        critical_section_init(&criticalSection_);
        interruptTimeProvider_ = interruptTimeProvider;
        initialized_ = true;
    }

    [[nodiscard]] bool trySubmit(
        Function function, void* context, JobCounter* counter = nullptr,
        Category category = Category::Other) noexcept {
        if (!initialized_ || function == nullptr) return false;

        critical_section_enter_blocking(&criticalSection_);
        if (queuedCount_ == jobs_.size()) {
            critical_section_exit(&criticalSection_);
            return false;
        }
        if (counter != nullptr) {
            counter->remaining_.fetch_add(1, std::memory_order_relaxed);
        }
        jobs_[writePosition_] = {function, context, counter, category};
        writePosition_ = (writePosition_ + 1U) & kIndexMask;
        ++queuedCount_;
        critical_section_exit(&criticalSection_);
        __sev();
        return true;
    }

    [[nodiscard]] bool tryRunOne() noexcept {
        if (!initialized_) return false;

        Job job{};
        critical_section_enter_blocking(&criticalSection_);
        if (queuedCount_ == 0) {
            critical_section_exit(&criticalSection_);
            return false;
        }
        job = jobs_[readPosition_];
        readPosition_ = (readPosition_ + 1U) & kIndexMask;
        --queuedCount_;
        critical_section_exit(&criticalSection_);

        const auto interruptStart =
            interruptTimeProvider_ != nullptr ? interruptTimeProvider_() : 0;
        const auto startedAt = time_us_32();
        job.function(job.context);
        const auto wallUs = time_us_32() - startedAt;
        const auto interruptUs = interruptTimeProvider_ != nullptr
            ? interruptTimeProvider_() - interruptStart : 0;
        const auto elapsedUs = wallUs > interruptUs ? wallUs - interruptUs : 0;
        const auto core = get_core_num();
        const auto category = static_cast<std::size_t>(job.category);
        profileUs_[core][category].fetch_add(elapsedUs, std::memory_order_relaxed);
        if (job.counter != nullptr
            && job.counter->remaining_.fetch_sub(1, std::memory_order_release) == 1) {
            __sev();
        }
        return true;
    }

    [[nodiscard]] Profile takeProfile(std::size_t core) noexcept {
        if (core >= profileUs_.size()) return {};
        return {
            profileUs_[core][static_cast<std::size_t>(Category::Render)].exchange(
                0, std::memory_order_acq_rel),
            profileUs_[core][static_cast<std::size_t>(Category::Audio)].exchange(
                0, std::memory_order_acq_rel),
            profileUs_[core][static_cast<std::size_t>(Category::Game)].exchange(
                0, std::memory_order_acq_rel),
            profileUs_[core][static_cast<std::size_t>(Category::Other)].exchange(
                0, std::memory_order_acq_rel),
        };
    }

    void wait(JobCounter& counter) noexcept {
        while (!counter.complete()) {
            if (!tryRunOne()) __wfe();
        }
    }

private:
    struct Job {
        Function function = nullptr;
        void* context = nullptr;
        JobCounter* counter = nullptr;
        Category category = Category::Other;
    };

    static constexpr std::size_t kCapacity = 32;
    static constexpr std::size_t kIndexMask = kCapacity - 1U;
    static_assert((kCapacity & kIndexMask) == 0,
                  "ジョブキュー容量は2の冪でなければなりません");

    std::array<Job, kCapacity> jobs_{};
    critical_section_t criticalSection_{};
    std::size_t readPosition_ = 0;
    std::size_t writePosition_ = 0;
    std::size_t queuedCount_ = 0;
    std::array<
        std::array<std::atomic<std::uint32_t>, static_cast<std::size_t>(Category::Count)>,
        2> profileUs_{};
    InterruptTimeProvider interruptTimeProvider_ = nullptr;
    bool initialized_ = false;
};

} // namespace wizward::rp2350
