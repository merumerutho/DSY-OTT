#pragma once

#include "biquad.h"
#include "fast_math.h"
#include "settings.h"
#include <math.h>

namespace ott {

// Per-notch harmonic noise gate.
//
// For each entry in settings::notch::kNotches[]:
//   - A stereo biquad notch attenuates the tone.
//   - A mono biquad bandpass at the same frequency (wider Q, controlled
//     by settings::notch::kSidechainQRatio) drives an envelope follower.
//     When that local energy is low the notch engages fully; when it is
//     high, psychoacoustic masking handles the noise so the notch backs
//     off. The depth is crossfaded between dry and notched.
//
// One user-facing knob: SetDepth(0..1) scales every notch's dynamic depth
// by the same amount. Knob at 0 -> notches always inactive (the biquads
// still run so state stays warm and re-engagement is click-free); knob at
// 1 -> every notch can reach full strength when its sidechain says so.
//
// Layout: templated on N so the storage and per-sample loop size both
// follow settings::notch::kNumNotches automatically. Edit the array in
// settings.h to add/remove notches; everything else updates at compile
// time.
//
// Placement: between the broadband NoiseGate and the crossover. The
// NoiseGate kills the deepest noise floor; this module attenuates the
// tonal residue that sneaks through when the gate is open but the
// signal is too quiet to mask the harmonics psychoacoustically.
template<int N>
class HarmonicGate {
  public:
    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        env_attack_coeff_   = OnePoleCoeff(settings::notch::kEnvAttackS);
        env_release_coeff_  = OnePoleCoeff(settings::notch::kEnvReleaseS);
        depth_smooth_coeff_ = OnePoleCoeff(settings::notch::kDepthSmoothS);

        for (int i = 0; i < N; ++i) {
            const float fc  = settings::notch::kNotches[i].freq;
            const float Q   = settings::notch::kNotches[i].Q;
            const bool  dyn = settings::notch::kNotches[i].sidechain_controlled;

            notch_l_[i].SetNotch(fc, Q, sample_rate);
            notch_r_[i].SetNotch(fc, Q, sample_rate);

            if (dyn) {
                const float sc_Q = Q * settings::notch::kSidechainQRatio;
                sidechain_[i].SetBandpass(fc, sc_Q, sample_rate);
                env_[i]   = 0.f;
                depth_[i] = 0.f;     // starts disengaged; sidechain ramps up
            } else {
                // Static notch: depth held at 1 forever, only the user
                // knob modulates it.
                env_[i]   = 0.f;
                depth_[i] = 1.f;
            }
        }
    }

    // 0..1. 0 = harmonic gate effectively bypassed (notches still run to
    // keep filter state warm but mix coefficient is 0). 1 = each notch
    // can reach full strength when its sidechain envelope is low.
    void SetDepth(float d) {
        if (d < 0.f) d = 0.f; else if (d > 1.f) d = 1.f;
        depth_user_ = d;
    }

    // Process one stereo sample in-place.
    inline void Process(float& l, float& r) {
        const float in_mono = 0.5f * (l + r);

        for (int i = 0; i < N; ++i) {
            // Sidechain-controlled notches compute a dynamic depth from
            // local energy. Static notches skip this entirely; their
            // depth_[i] stays at 1.0 (set in Init).
            if (settings::notch::kNotches[i].sidechain_controlled) {
                // Sidechain BPF -> rectified envelope follower (mono).
                const float bp    = fabsf(sidechain_[i].Process(in_mono));
                const float coeff = (bp > env_[i]) ? env_attack_coeff_
                                                   : env_release_coeff_;
                env_[i] += (bp - env_[i]) * coeff;

                // Map envelope (in dB) to a target depth.
                //   <= kDepthLowDb  -> 1 (fully engaged)
                //   >= kDepthHighDb -> 0 (bypassed; masking takes over)
                //   linear ramp between.
                const float env_db = FastDbFromLinear(env_[i] + 1e-10f);
                float target;
                if (env_db <= settings::notch::kDepthLowDb) {
                    target = 1.f;
                } else if (env_db >= settings::notch::kDepthHighDb) {
                    target = 0.f;
                } else {
                    target = (settings::notch::kDepthHighDb - env_db)
                           / (settings::notch::kDepthHighDb
                            - settings::notch::kDepthLowDb);
                }
                depth_[i] += (target - depth_[i]) * depth_smooth_coeff_;
            }

            // Notch biquads always run -- state stays warm regardless of
            // the mix coefficient, so re-engagement after a long bypass
            // doesn't pop. Final mix is dry crossfaded toward notched by
            // the user knob times the per-notch depth.
            const float wet_l = notch_l_[i].Process(l);
            const float wet_r = notch_r_[i].Process(r);
            const float d     = depth_user_ * depth_[i];
            l += (wet_l - l) * d;
            r += (wet_r - r) * d;
        }
    }

  private:
    inline float OnePoleCoeff(float tau_s) const {
        if (tau_s <= 0.f) return 1.f;
        return 1.f - expf(-1.f / (sample_rate_ * tau_s));
    }

    Biquad notch_l_[N];
    Biquad notch_r_[N];
    Biquad sidechain_[N];
    float  env_[N]   = {};
    float  depth_[N] = {};

    float env_attack_coeff_   = 0.f;
    float env_release_coeff_  = 0.f;
    float depth_smooth_coeff_ = 0.f;

    float depth_user_  = 0.f;
    float sample_rate_ = 48000.f;
};

} // namespace ott
