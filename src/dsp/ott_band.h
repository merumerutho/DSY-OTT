#pragma once

#include "fast_math.h"
#include "settings.h"
#include <math.h>

namespace ott {

// Per-band processor: stereo-linked peak envelope follower + up/down gain
// computer + makeup gain. One instance per crossover band.
//
// OTT character: above threshold the band is pulled down, below threshold the
// band is pushed up, both at a moderate ratio. The undershoot is capped so the
// noise floor doesn't explode when the band is silent.
//
// Design choices:
//   - Peak detection (max(|L|, |R|)) rather than RMS. Each band already has
//     limited dynamic range after splitting, so RMS smoothing here would be
//     sluggish and pump weirdly. Peak matches classic OTT behaviour.
//   - Asymmetric one-pole envelope: separate attack and release coefficients
//     selected per sample by comparing the rectified input against the current
//     envelope value.
//   - Stereo-linked gain: a single gain value applied to both channels keeps
//     the stereo image stable.
//   - dB-domain gain computation. Per-sample log10f + powf is cheap on the M7
//     and avoids fragile small-number arithmetic.
class OttBand {
  public:
    // Threshold/makeup ranges, ratio, time constants and time-mult scale all
    // live in settings::band so the user can tune them in one place.

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        SetThreshold(0.5f);
        SetMakeup(0.f);
        SetTimeMult(0.5f);
        env_ = 0.f;
    }

    // Backwards-compat alias for the earlier scaffold.
    void SetSampleRate(float sample_rate) { Init(sample_rate); }

    // 0..1 normalised. Linear in dB across the configured threshold range.
    void SetThreshold(float t) {
        if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
        thresh_db_ = settings::band::kThreshMinDb
                   + (settings::band::kThreshMaxDb - settings::band::kThreshMinDb) * t;
    }

    // 0..1 normalised. 0 = unity, 1 = +settings::band::kMakeupMaxDb dB.
    void SetMakeup(float m) {
        if (m < 0.f) m = 0.f; else if (m > 1.f) m = 1.f;
        makeup_db_ = settings::band::kMakeupMaxDb * m;
    }

    // 0..1 normalised. 0.5 = nominal speed; range is 2^(+-kTimeMultExpRange/2).
    void SetTimeMult(float tm) {
        if (tm < 0.f) tm = 0.f; else if (tm > 1.f) tm = 1.f;
        const float scale = exp2f(settings::band::kTimeMultExpRange * (tm - 0.5f));
        const float att_s = settings::band::kNominalAttackS  * scale;
        const float rel_s = settings::band::kNominalReleaseS * scale;
        attack_coeff_  = OnePoleCoeff(att_s);
        release_coeff_ = OnePoleCoeff(rel_s);
    }

    // Process one stereo sample in-place.
    inline void Process(float& l, float& r) {
        // Stereo-linked peak detection.
        const float abs_l  = fabsf(l);
        const float abs_r  = fabsf(r);
        const float in_abs = (abs_l > abs_r) ? abs_l : abs_r;

        // Asymmetric one-pole envelope: rise vs fall use different time
        // constants so we can react fast to peaks but recover slowly.
        const float coeff = (in_abs > env_) ? attack_coeff_ : release_coeff_;
        env_ += (in_abs - env_) * coeff;

        // dB-domain gain computation. FastDbFromLinear / FastLinearFromDb
        // use IEEE-754 bit tricks instead of libm log10f / powf -- ~3-7x
        // cheaper at < 0.001 dB error.
        const float env_db = FastDbFromLinear(env_ + 1e-10f);
        const float over   = env_db - thresh_db_; // > 0 above thresh
        constexpr float kRatioFactor = 1.f - 1.f / settings::band::kRatio;

        float gr_db;
        if (over > 0.f) {
            // Above threshold: downward compression.
            gr_db = -over * kRatioFactor;
        } else {
            // Below threshold: upward compression with hard knee at the cap.
            float under = -over;
            if (under > settings::band::kBoostCapDb)
                under = settings::band::kBoostCapDb;
            gr_db = +under * kRatioFactor;
        }

        const float gain_db = gr_db + makeup_db_;
        const float gain    = FastLinearFromDb(gain_db);

        l *= gain;
        r *= gain;
    }

    // Pre-gain peak envelope of this band's input. Useful as a sidechain
    // source: tracks the band's signal level with the same time constants
    // the compressor itself uses, so a CV derived from this follows whatever
    // the user has dialled in for time_mult.
    inline float Envelope() const { return env_; }

  private:
    // First-order lowpass coefficient for time constant tau seconds:
    //   coeff = 1 - exp(-1 / (sr * tau))
    // After ~tau seconds, env reaches ~63% of the input step.
    inline float OnePoleCoeff(float tau_s) const {
        if (tau_s <= 0.f) return 1.f;
        return 1.f - expf(-1.f / (sample_rate_ * tau_s));
    }

    float sample_rate_   = 48000.f;
    float thresh_db_     = -30.f;
    float makeup_db_     = 0.f;
    float attack_coeff_  = 0.f;
    float release_coeff_ = 0.f;
    float env_           = 0.f;
};

} // namespace ott
