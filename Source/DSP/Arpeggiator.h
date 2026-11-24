// Header-only tempo-synced arpeggiator running at the canonical 88.2 kHz rate.
#pragma once

#include "Core.h"
#include "TranceraPatch.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <utility>

namespace trancera
{
class Arpeggiator
{
public:
    struct Event
    {
        int offNote { -1 };
        int onNote { -1 };
        int onVel { 0 };
    };

    void reset (std::uint32_t seed) noexcept
    {
        count_ = 0;
        playingNote_ = -1;
        stepIndex_ = 0;
        stepRemain_ = 0;
        gateRemain_ = 0;
        order_ = 0;
        rng_ = seed ? seed : 1u;
        stepTicks_ = computeStepTicks();
    }

    void clear() noexcept
    {
        count_ = 0;
        playingNote_ = -1;
        stepIndex_ = 0;
        stepRemain_ = 0;
        gateRemain_ = 0;
    }

    void setTempo (double bpm) noexcept
    {
        bpm_ = std::clamp (bpm, 20.0, 300.0);
        stepTicks_ = computeStepTicks();
        if (active() && stepRemain_ > stepTicks_)
            stepRemain_ = stepTicks_;
        if (playingNote_ >= 0 && gateRemain_ > stepTicks_ - 1)
            gateRemain_ = std::max (1, stepTicks_ - 1);
    }

    void configure (const ArpParams& params, bool sustainDown) noexcept
    {
        params_ = params;
        params_.range = std::clamp (params_.range, 0, 3);
        params_.rate = std::clamp (params_.rate, 0, 7);
        params_.gate = std::clamp (params_.gate, 10, 100);
        stepTicks_ = computeStepTicks();
        if (! params_.hold && ! sustainDown)
            removeUnheld();
        if (active() && stepRemain_ > stepTicks_)
            stepRemain_ = stepTicks_;
        if (playingNote_ >= 0 && gateRemain_ > stepTicks_ - 1)
            gateRemain_ = std::max (1, stepTicks_ - 1);
    }

    void noteOn (int note, int velocity, bool sustainDown) noexcept
    {
        if (note < 0 || note > 127 || velocity <= 0)
            return;
        if (! hasPhysicalHeld() && ! sustainDown && count_ > 0)
        {
            count_ = 0;
            stepIndex_ = 0;
            stepRemain_ = 0;
        }
        const bool wasEmpty = count_ == 0;
        insertOrUpdate (note, velocity);
        if (wasEmpty)
        {
            stepIndex_ = 0;
            stepRemain_ = 0;
            gateRemain_ = 0;
        }
    }

    void noteOff (int note, bool sustainDown) noexcept
    {
        for (int i = 0; i < count_; ++i)
        {
            if (pool_[static_cast<std::size_t> (i)].note != note)
                continue;
            pool_[static_cast<std::size_t> (i)].held = false;
            if (sustainDown)
                pool_[static_cast<std::size_t> (i)].sustained = true;
            else if (! params_.hold)
                eraseAt (i);
            break;
        }
    }

    void setSustain (bool down) noexcept
    {
        if (down)
            return;
        for (int i = 0; i < count_; ++i)
            pool_[static_cast<std::size_t> (i)].sustained = false;
        if (! params_.hold)
            removeUnheld();
    }

    void seedHeld (int note, int velocity) noexcept
    {
        if (note < 0 || note > 127 || velocity <= 0)
            return;
        const bool wasEmpty = count_ == 0;
        insertOrUpdate (note, velocity);
        if (wasEmpty)
        {
            stepIndex_ = 0;
            stepRemain_ = 0;
            gateRemain_ = 0;
        }
    }

    [[nodiscard]] int count() const noexcept { return count_; }
    [[nodiscard]] bool active() const noexcept { return params_.on && (count_ > 0 || playingNote_ >= 0); }
    [[nodiscard]] int playingNote() const noexcept { return playingNote_; }
    void clearPlayingNote() noexcept { playingNote_ = -1; }

    [[nodiscard]] int ticksUntilEvent() const noexcept
    {
        if (! active())
            return std::numeric_limits<int>::max();
        if (count_ == 0 && playingNote_ >= 0)
            return 0;
        int next = stepRemain_;
        if (playingNote_ >= 0)
            next = std::min (next, gateRemain_);
        return std::max (0, next);
    }

    Event advance (int ticks) noexcept
    {
        Event ev;
        if (! active())
            return ev;
        stepRemain_ = std::max (0, stepRemain_ - ticks);
        if (playingNote_ >= 0)
        {
            gateRemain_ = std::max (0, gateRemain_ - ticks);
            if (gateRemain_ == 0)
            {
                ev.offNote = playingNote_;
                playingNote_ = -1;
            }
        }
        if (count_ == 0)
            return ev;
        if (stepRemain_ == 0)
        {
            std::array<std::pair<int, int>, 128> seq {};
            const int seqLen = buildSequence (seq);
            if (seqLen > 0)
            {
                const int idx = pickIndex (seqLen);
                if (playingNote_ >= 0)
                    ev.offNote = playingNote_;
                playingNote_ = seq[static_cast<std::size_t> (idx)].first;
                ev.onNote = playingNote_;
                ev.onVel = seq[static_cast<std::size_t> (idx)].second;
                ++stepIndex_;
                stepRemain_ = stepTicks_;
                gateRemain_ = computeGateTicks();
            }
        }
        return ev;
    }

private:
    struct Entry
    {
        int note { 0 };
        int velocity { 0 };
        std::uint64_t order { 0 };
        bool held { false };
        bool sustained { false };
    };

    static constexpr double kBeats[8] = {
        1.0, 0.75, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125
    };

    [[nodiscard]] int computeStepTicks() const noexcept
    {
        const auto idx = static_cast<std::size_t> (std::clamp (params_.rate, 0, 7));
        return std::max (64, static_cast<int> (kEngineRate * (60.0 / bpm_) * kBeats[idx] + 0.5));
    }

    [[nodiscard]] int computeGateTicks() const noexcept
    {
        const int gatePct = std::clamp (params_.gate, 10, 100);
        return std::clamp ((stepTicks_ * gatePct + 50) / 100, 1, std::max (1, stepTicks_ - 1));
    }

    [[nodiscard]] bool hasPhysicalHeld() const noexcept
    {
        for (int i = 0; i < count_; ++i)
            if (pool_[static_cast<std::size_t> (i)].held)
                return true;
        return false;
    }

    void eraseAt (int index) noexcept
    {
        for (int j = index + 1; j < count_; ++j)
            pool_[static_cast<std::size_t> (j - 1)] = pool_[static_cast<std::size_t> (j)];
        if (count_ > 0)
            --count_;
    }

    void removeUnheld() noexcept
    {
        int kept = 0;
        for (int i = 0; i < count_; ++i)
        {
            const Entry& e = pool_[static_cast<std::size_t> (i)];
            if (e.held || e.sustained)
                pool_[static_cast<std::size_t> (kept++)] = e;
        }
        count_ = kept;
    }

    void insertOrUpdate (int note, int velocity) noexcept
    {
        for (int i = 0; i < count_; ++i)
        {
            Entry& e = pool_[static_cast<std::size_t> (i)];
            if (e.note == note)
            {
                e.velocity = velocity;
                e.order = ++order_;
                e.held = true;
                e.sustained = false;
                return;
            }
        }
        if (count_ >= static_cast<int> (pool_.size()))
        {
            int oldest = 0;
            for (int i = 1; i < count_; ++i)
                if (pool_[static_cast<std::size_t> (i)].order < pool_[static_cast<std::size_t> (oldest)].order)
                    oldest = i;
            eraseAt (oldest);
        }
        pool_[static_cast<std::size_t> (count_++)] = { note, velocity, ++order_, true, false };
    }

    [[nodiscard]] int buildSequence (std::array<std::pair<int, int>, 128>& out) const noexcept
    {
        std::array<Entry, 32> base {};
        for (int i = 0; i < count_; ++i)
            base[static_cast<std::size_t> (i)] = pool_[static_cast<std::size_t> (i)];

        const bool asPlayed = params_.mode == ArpMode::AsPlayed;
        std::sort (base.begin(), base.begin() + count_, [asPlayed] (const Entry& a, const Entry& b) {
            return asPlayed ? a.order < b.order : a.note < b.note;
        });

        const int octaves = std::clamp (params_.range, 0, 3) + 1;
        int len = 0;
        for (int oct = 0; oct < octaves && len < static_cast<int> (out.size()); ++oct)
        {
            for (int i = 0; i < count_ && len < static_cast<int> (out.size()); ++i)
            {
                const int shifted = base[static_cast<std::size_t> (i)].note + oct * 12;
                if (shifted >= 0 && shifted <= 127)
                    out[static_cast<std::size_t> (len++)] = { shifted, base[static_cast<std::size_t> (i)].velocity };
            }
        }
        return len;
    }

    [[nodiscard]] int pickIndex (int n) noexcept
    {
        const int pos = stepIndex_ % n;
        switch (params_.mode)
        {
            case ArpMode::Up:
            case ArpMode::AsPlayed:
                return pos;
            case ArpMode::Down:
                return n - 1 - pos;
            case ArpMode::UpDown:
            {
                const int spanLen = n <= 2 ? n : (2 * n - 2);
                const int p = stepIndex_ % spanLen;
                return p < n ? p : (spanLen - p);
            }
            case ArpMode::Random:
                rng_ = rng_ * 1664525u + 1013904223u;
                return static_cast<int> ((rng_ >> 16) % static_cast<std::uint32_t> (n));
        }
        return pos;
    }

    std::array<Entry, 32> pool_ {};
    int count_ { 0 };
    ArpParams params_ {};
    double bpm_ { 120.0 };
    int stepTicks_ { 11025 };
    int playingNote_ { -1 };
    int stepIndex_ { 0 };
    int stepRemain_ { 0 };
    int gateRemain_ { 0 };
    std::uint64_t order_ { 0 };
    std::uint32_t rng_ { 1u };
};
} // namespace trancera
