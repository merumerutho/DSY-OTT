#pragma once

#include "biquad.h"
#include "settings.h"

namespace ott {

// 4-band Linkwitz-Riley 4th-order crossover (stereo).
//
// Topology: hierarchical tree split with LR4 crossovers at three frequencies.
//   stage A:  full input  -> [LR4 @ f2] -> low_half  / high_half
//   stage B:  low_half    -> [LR4 @ f1] -> Low      / Lo-Mid
//   stage C:  high_half   -> [LR4 @ f3] -> Mid-Hi   / High
//
// Each LR4-LP / LR4-HP is two cascaded Butterworth 2nd-order biquads at the
// same cutoff. Linkwitz-Riley LP+HP at the same cutoff sum to a unity-magnitude
// allpass; chained as a tree the four bands sum (with some allpass phase warp)
// to a magnitude-flat replica of the input. Phase artefacts at crossover
// points are masked when each band is dynamically processed.
class Crossover {
  public:
    static constexpr int kNumBands = 4;

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        SetCrossovers(settings::crossover::kLowMidHz,
                      settings::crossover::kMidMidHz,
                      settings::crossover::kMidHiHz);
        for (auto& s : stages_) {
            for (int ch = 0; ch < 2; ++ch) {
                s.lp1[ch].Reset();
                s.lp2[ch].Reset();
                s.hp1[ch].Reset();
                s.hp2[ch].Reset();
            }
        }
    }

    // Backwards-compat alias for the earlier scaffold; just delegates.
    void SetSampleRate(float sample_rate) { Init(sample_rate); }

    // f1 < f2 < f3 (Hz). Updated cutoffs take effect immediately.
    void SetCrossovers(float f1, float f2, float f3) {
        SetStageFreq(0, f1); // stage B (low_half  -> Low / LoMid)
        SetStageFreq(1, f2); // stage A (full      -> low_half / high_half)
        SetStageFreq(2, f3); // stage C (high_half -> MidHi / High)
    }

    // Process one stereo sample. out_l/out_r ordered [Low, LoMid, MidHi, High].
    void Process(float in_l, float in_r,
                 float* out_l, float* out_r) {
        // Stage A: split full input at f2.
        float low_half_l, high_half_l, low_half_r, high_half_r;
        SplitLR4(stages_[1], 0, in_l, low_half_l, high_half_l);
        SplitLR4(stages_[1], 1, in_r, low_half_r, high_half_r);

        // Stage B: split low_half at f1 -> Low (band 0), LoMid (band 1).
        SplitLR4(stages_[0], 0, low_half_l, out_l[0], out_l[1]);
        SplitLR4(stages_[0], 1, low_half_r, out_r[0], out_r[1]);

        // Stage C: split high_half at f3 -> MidHi (band 2), High (band 3).
        SplitLR4(stages_[2], 0, high_half_l, out_l[2], out_l[3]);
        SplitLR4(stages_[2], 1, high_half_r, out_r[2], out_r[3]);
    }

  private:
    static constexpr int kStages = 3;

    struct Stage {
        Biquad lp1[2], lp2[2]; // LR4-LP cascade (per channel)
        Biquad hp1[2], hp2[2]; // LR4-HP cascade (per channel)
    };

    void SetStageFreq(int stage, float f) {
        for (int ch = 0; ch < 2; ++ch) {
            stages_[stage].lp1[ch].Set(Biquad::Type::Lowpass,  f, sample_rate_);
            stages_[stage].lp2[ch].Set(Biquad::Type::Lowpass,  f, sample_rate_);
            stages_[stage].hp1[ch].Set(Biquad::Type::Highpass, f, sample_rate_);
            stages_[stage].hp2[ch].Set(Biquad::Type::Highpass, f, sample_rate_);
        }
    }

    static inline void SplitLR4(Stage& s, int ch, float in,
                                float& out_lp, float& out_hp) {
        // LR4-LP = LP biquad followed by LP biquad (both Butterworth).
        const float lp1 = s.lp1[ch].Process(in);
        out_lp = s.lp2[ch].Process(lp1);

        // LR4-HP = HP biquad followed by HP biquad (both Butterworth).
        const float hp1 = s.hp1[ch].Process(in);
        out_hp = s.hp2[ch].Process(hp1);
    }

    Stage stages_[kStages];
    float sample_rate_ = 48000.f;
};

} // namespace ott
