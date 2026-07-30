#pragma once

#include "pico/critical_section.h"
#include "pico/platform.h"

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
    // contextの寿命は完了カウンタが0になるまで呼び出し側が保証する。
    // LEDのフレーム境界を遅延させない、短時間かつ非ブロッキングな処理だけを投入する。
    using Function = void (*)(void*) noexcept;

    void initialize() noexcept {
        critical_section_init(&criticalSection_);
        initialized_ = true;
    }

    [[nodiscard]] bool trySubmit(
        Function function, void* context, JobCounter* counter = nullptr) noexcept {
        if (!initialized_ || function == nullptr) return false;

        critical_section_enter_blocking(&criticalSection_);
        if (queuedCount_ == jobs_.size()) {
            critical_section_exit(&criticalSection_);
            return false;
        }
        if (counter != nullptr) {
            counter->remaining_.fetch_add(1, std::memory_order_relaxed);
        }
        jobs_[writePosition_] = {function, context, counter};
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

        job.function(job.context);
        if (job.counter != nullptr
            && job.counter->remaining_.fetch_sub(1, std::memory_order_release) == 1) {
            __sev();
        }
        return true;
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
    bool initialized_ = false;
};

} // namespace wizward::rp2350
