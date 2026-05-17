#pragma once

#include "fast_math.h"
#include "settings.h"
#include <math.h>

namespace ott {

// Stereo-linked noise gate sitting at the very input of the chain.
//
// One user-facing knob: threshold. The rest (attack, release, hold,
// hysteresis) are hardcoded to values that work well for typical "kill
// audible noise floor on a slightly noisy input" use. Knob at 0 = bypass.
//
// State machine:
//   closed: env_db <= open_thresh_db; gain ramps toward 0 with release coeff.
//   open:   env_db > open_thresh_db at any point in the recent past;
//           gain ramps toward 1 with attack coeff. Stays open while
//           env_db > close_thresh_db (hysteresis), and remains open for an
//           additional `hold` window after the level falls below
//           close_thresh_db, to avoid chattering on natural decays.
//
// Detection mirrors the band processor: max(|L|, |R|) -> fast peak follower.
class NoiseGate {
  public:
    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        env_attack_coeff_   = OnePoleCoeff(settings::gate::kEnvAttackS);
        env_release_coeff_  = OnePoleCoeff(settings::gate::kEnvReleaseS);
        attack_coeff_       = OnePoleCoeff(settings::gate::kAttackS);
        release_coeff_      = OnePoleCoeff(settings::gate::kReleaseS);
        hold_total_samples_ = static_cast<int>(settings::gate::kHoldS * sample_rate);
        SetThreshold(0.f);
        env_                    = 0.f;
        gain_                   = 1.f;
        open_                   = false;
        hold_remaining_samples_ = 0;
    }

    // 0..1 normalised. Below settings::gate::kBypassDeadzone the gate is
    // bypassed entirely (passthrough). Above, the open threshold sweeps
    // from kThreshMinDb up to kThreshMaxDb (linear in dB).
    void SetThreshold(float t) {
        if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
        bypassed_ = (t < settings::gate::kBypassDeadzone);
        if (!bypassed_) {
            const float k = (t - settings::gate::kBypassDeadzone)
                          / (1.f - settings::gate::kBypassDeadzone);
            open_thresh_db_ = settings::gate::kThreshMinDb
                            + (settings::gate::kThreshMaxDb
                             - settings::gate::kThreshMinDb) * k;
            close_thresh_db_ = open_thresh_db_ - settings::gate::kHysteresisDb;
        }
    }

    // Process one stereo sample in-place.
    inline void Process(float& l, float& r) {
        if (bypassed_) {
            // Snap any in-flight closing state back to wide open so re-enabling
            // the gate later doesn't suddenly attenuate.
            gain_  = 1.f;
            open_  = true;
            return;
        }

        const float abs_l  = fabsf(l);
        const float abs_r  = fabsf(r);
        const float in_abs = (abs_l > abs_r) ? abs_l : abs_r;

        const float env_coeff = (in_abs > env_) ? env_attack_coeff_
                                                : env_release_coeff_;
        env_ += (in_abs - env_) * env_coeff;

        const float env_db = FastDbFromLinear(env_ + 1e-10f);

        if (open_) {
            if (env_db < close_thresh_db_) {
                if (hold_remaining_samples_ > 0) {
                    --hold_remaining_samples_;
                } else {
                    open_ = false;
                }
            } else {
                hold_remaining_samples_ = hold_total_samples_;
            }
        } else {
            if (env_db > open_thresh_db_) {
                open_ = true;
                hold_remaining_samples_ = hold_total_samples_;
            }
        }

        const float target = open_ ? 1.f : 0.f;
        const float coeff  = (target > gain_) ? attack_coeff_ : release_coeff_;
        gain_ += (target - gain_) * coeff;

        l *= gain_;
        r *= gain_;
    }

  private:
    inline float OnePoleCoeff(float tau_s) const {
        if (tau_s <= 0.f) return 1.f;
        return 1.f - expf(-1.f / (sample_rate_ * tau_s));
    }

    float sample_rate_       = 48000.f;
    float open_thresh_db_    = settings::gate::kThreshMinDb;
    float close_thresh_db_   = settings::gate::kThreshMinDb
                             - settings::gate::kHysteresisDb;
    float env_               = 0.f;
    float gain_              = 1.f;
    bool  open_              = false;
    bool  bypassed_          = true;
    int   hold_remaining_samples_ = 0;
    int   hold_total_samples_     = 0;
    float env_attack_coeff_  = 0.f;
    float env_release_coeff_ = 0.f;
    float attack_coeff_      = 0.f;
    float release_coeff_     = 0.f;
};

} // namespace ott
