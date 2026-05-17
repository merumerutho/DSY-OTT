#pragma once

#include "biquad.h"
#include "settings.h"

namespace ott {

// Stereo 4-band post EQ: sub low shelf, bass bell, mid bell, high shelf,
// all with adjustable gain. Sits at the very end of the chain (after
// output gain, before soft clip) so it shapes the final tone of the
// processed sum. One band per OTT crossover region (Sub / Bass / Mid /
// High).
//
// Frequencies are fixed; only the gain per band is user-controllable. This
// is an intentional simplification -- four knobs, four colours.
class PostEq {
  public:
    // Frequencies & gain range live in settings::posteq so the user can
    // edit them in one place without diving into the DSP module.

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        // Force a coefficient compute on first SetGainsNorm by choosing
        // an impossible "previous" value.
        sub_db_  = 1e9f;
        bass_db_ = 1e9f;
        mid_db_  = 1e9f;
        high_db_ = 1e9f;
        SetGainsNorm(0.5f, 0.5f, 0.5f, 0.5f);
        for (int ch = 0; ch < 2; ++ch) {
            sub_[ch].Reset();
            bass_[ch].Reset();
            mid_[ch].Reset();
            high_[ch].Reset();
        }
    }

    // 0..1 normalised per band. 0 = -12 dB, 0.5 = unity, 1 = +12 dB.
    // Re-computes biquad coefficients only for bands whose gain has
    // actually changed.
    void SetGainsNorm(float sub_n, float bass_n, float mid_n, float high_n) {
        const float s_db = NormToGainDb(sub_n);
        const float b_db = NormToGainDb(bass_n);
        const float m_db = NormToGainDb(mid_n);
        const float h_db = NormToGainDb(high_n);

        if (s_db != sub_db_) {
            sub_db_ = s_db;
            for (int ch = 0; ch < 2; ++ch)
                sub_[ch].SetLowShelf(settings::posteq::kSubFc, s_db, sample_rate_);
        }
        if (b_db != bass_db_) {
            bass_db_ = b_db;
            for (int ch = 0; ch < 2; ++ch)
                bass_[ch].SetPeaking(settings::posteq::kBassFc, b_db,
                                     settings::posteq::kBassQ, sample_rate_);
        }
        if (m_db != mid_db_) {
            mid_db_ = m_db;
            for (int ch = 0; ch < 2; ++ch)
                mid_[ch].SetPeaking(settings::posteq::kMidFc, m_db,
                                    settings::posteq::kMidQ, sample_rate_);
        }
        if (h_db != high_db_) {
            high_db_ = h_db;
            for (int ch = 0; ch < 2; ++ch)
                high_[ch].SetHighShelf(settings::posteq::kHighFc, h_db, sample_rate_);
        }
    }

    inline void Process(float& l, float& r) {
        l = sub_[0].Process(l);
        r = sub_[1].Process(r);
        l = bass_[0].Process(l);
        r = bass_[1].Process(r);
        l = mid_[0].Process(l);
        r = mid_[1].Process(r);
        l = high_[0].Process(l);
        r = high_[1].Process(r);
    }

  private:
    static inline float NormToGainDb(float n) {
        if (n < 0.f) n = 0.f; else if (n > 1.f) n = 1.f;
        const float r = settings::posteq::kGainRangeDb;
        return -r + 2.f * r * n; // 0 -> -r, 0.5 -> 0, 1 -> +r
    }

    Biquad sub_[2], bass_[2], mid_[2], high_[2];
    float  sample_rate_ = 48000.f;
    float  sub_db_      = 0.f;
    float  bass_db_     = 0.f;
    float  mid_db_      = 0.f;
    float  high_db_     = 0.f;
};

} // namespace ott
