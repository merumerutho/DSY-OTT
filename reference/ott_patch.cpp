// OTT-style multiband effect for the Electrosmith patch.Init() module.
//
// Signal chain:
//   in -> input_gain -> harmonic_gate -> noise_gate -> (split 4 bands)
//      -> per-band up/down -> sum -> [depth crossfade vs post-gate dry]
//      -> output_gain -> post_eq (3-band) -> cv_duck -> soft_clip -> out
//
// The harmonic gate removes stable tonal noise (hum, clock leakage,
// SMPS whine) before the broadband noise gate so the gate's threshold
// detection sees a cleaner residual floor.
//
// CV out 1 = sidechain output from Low band envelope.
// CV in (configured jack) = sidechain ducking input.

#include "daisy_patch_sm.h"
#include "sys/system.h"
#include "controls.h"
#include "gate.h"
#include "harmonic_gate.h"
#include "crossover.h"
#include "ott_band.h"
#include "posteq.h"
#include "settings.h"
#include "fast_math.h"
#include <math.h>
#include <stdint.h>

using namespace daisy;
using namespace daisy::patch_sm;

static DaisyPatchSM   patch;
static ott::Controls  controls;
static ott::NoiseGate gate;
static ott::HarmonicGate<ott::settings::notch::kNumNotches> harmonic_gate;
static ott::Crossover crossover;
static ott::OttBand   bands[ott::Crossover::kNumBands];
static ott::PostEq    post_eq;

// Per-sample crossfade between processed (0) and bypass (1). One-pole at
// roughly 5 ms time constant @ 48 kHz keeps bypass toggling click-free even
// during loud audio.
static float bypass_mix_     = 0.f;
static constexpr float kBypassMixCoeff = 1.f / 240.f; // ~5 ms at 48 kHz

// LED PWM state. Counter increments per sample and wraps at kLedPwmPeriod;
// LED is on while counter < duty. ~500 Hz PWM at 48 kHz / 96-sample period.
static uint32_t led_pwm_counter_ = 0;

// Smoothed CV-input ducking gain. 1.0 = no ducking (unity), 0.0 = silent.
// Computed once per block from cv_in[settings::ducking::kCvJackIndex] and
// duck_depth, then smoothed per-sample with kDuckCoeff.
static float duck_gain_  = 1.f;
static float duck_coeff_ = 1.f; // recomputed in main() once sample rate is known

// Map normalised knob (0..1) to a linear gain spanning -24 dB .. +24 dB.
// 0 = -24 dB (~0.063x), 0.5 = unity, 1 = +24 dB (~16x).
static inline float NormToGainLinear(float knob) {
    if (knob < 0.f) knob = 0.f; else if (knob > 1.f) knob = 1.f;
    const float db = -24.f + 48.f * knob;
    return ott::FastLinearFromDb(db);
}

// Rational soft-clipper. Transparent for |x| < kKnee, smoothly saturates
// above, asymptotes to +-1. ~10 cycles per call (1 abs/branch + 1 div).
// Lives at the very end of the per-sample chain to keep loud transients
// (heavy makeup, depth at full, hot input, EQ boost) from hard-clipping at
// the DAC.
static inline float SoftClip(float x) {
    constexpr float kKnee  = 0.9f;
    constexpr float kRange = 1.f - kKnee; // 0.1
    if (x > kKnee) {
        const float over = x - kKnee;
        return kKnee + kRange * over / (over + kRange);
    }
    if (x < -kKnee) {
        const float over = -x - kKnee;
        return -(kKnee + kRange * over / (over + kRange));
    }
    return x;
}

// Convert the Low band envelope into a 0..5 V sidechain CV.
//   threshold_norm < kBypass   -> CV = 0 V (muted)
//   threshold_norm >= kBypass  -> threshold sweeps -60..0 dBFS;
//                                 CV climbs 0..5V over 12 dB above threshold.
static inline float SidechainCv(float low_env, float threshold_norm) {
    constexpr float kBypass    = 0.02f;
    constexpr float kThreshLo  = -60.f;  // dBFS
    constexpr float kThreshHi  =   0.f;
    constexpr float kRangeDb   =  12.f;  // dB above thresh -> full 5V
    if (threshold_norm < kBypass) return 0.f;

    const float k = (threshold_norm - kBypass) / (1.f - kBypass);
    const float thresh_db = kThreshLo + (kThreshHi - kThreshLo) * k;
    const float env_db    = ott::FastDbFromLinear(low_env + 1e-10f);
    float v = ((env_db - thresh_db) / kRangeDb) * 5.f;
    if (v < 0.f) v = 0.f; else if (v > 5.f) v = 5.f;
    return v;
}

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size) {
    patch.ProcessAllControls();
    controls.Process(patch);

    const ott::Params& p        = controls.params();
    const bool         bypassed = controls.Bypassed();

    gate.SetThreshold(p.gate_threshold);
    harmonic_gate.SetDepth(p.notch_depth);
    for (int b = 0; b < ott::Crossover::kNumBands; ++b) {
        bands[b].SetThreshold(p.threshold[b]);
        bands[b].SetMakeup(p.makeup[b]);
    }
    post_eq.SetGainsNorm(p.post_eq_low, p.post_eq_mid, p.post_eq_high);

    const float in_gain         = NormToGainLinear(p.input_gain);
    const float out_gain        = NormToGainLinear(p.output_gain);
    const float depth           = p.depth;
    const float dry_amt         = 1.f - depth;
    const float bypass_target   = bypassed ? 1.f : 0.f;
    const uint32_t led_duty     = controls.LedDuty(daisy::System::GetNow());

    // Resolve ducking target gain for this block. Reads the configured CV
    // jack, takes the positive portion above a small deadzone, scales by
    // the depth knob, and converts into a target gain in [1-depth, 1].
    const float cv_raw = p.cv_in[settings::ducking::kCvJackIndex];
    float cv_signal = (cv_raw - 0.5f) * 2.f; // -1..+1 (0V centred)
    if (cv_signal < settings::ducking::kDeadzone) cv_signal = 0.f;
    if (cv_signal > 1.f) cv_signal = 1.f;
    const float duck_amount = cv_signal * p.duck_depth; // 0..1
    const float duck_target = 1.f - duck_amount;

    for (size_t i = 0; i < size; ++i) {
        const float raw_l = in[0][i];
        const float raw_r = in[1][i];

        // DSP path runs even when bypassed, to keep envelope state warm
        // and avoid a transient on re-engagement.
        float l = raw_l * in_gain;
        float r = raw_r * in_gain;

        harmonic_gate.Process(l, r);
        gate.Process(l, r);

        const float dry_l = l;
        const float dry_r = r;

        float band_l[ott::Crossover::kNumBands];
        float band_r[ott::Crossover::kNumBands];
        crossover.Process(l, r, band_l, band_r);

        float wet_l = 0.f;
        float wet_r = 0.f;
        for (int b = 0; b < ott::Crossover::kNumBands; ++b) {
            bands[b].Process(band_l[b], band_r[b]);
            wet_l += band_l[b];
            wet_r += band_r[b];
        }

        float mix_l = (dry_l * dry_amt + wet_l * depth) * out_gain;
        float mix_r = (dry_r * dry_amt + wet_r * depth) * out_gain;

        post_eq.Process(mix_l, mix_r);

        // CV-input ducking: smooth toward target gain and apply. Affects
        // only the processed branch; the bypass crossfade below brings back
        // the raw input untouched, so true bypass is preserved.
        duck_gain_ += (duck_target - duck_gain_) * duck_coeff_;
        mix_l *= duck_gain_;
        mix_r *= duck_gain_;

        const float clipped_l = SoftClip(mix_l);
        const float clipped_r = SoftClip(mix_r);

        // Smoothly crossfade processed -> raw input on bypass enter/exit.
        bypass_mix_ += (bypass_target - bypass_mix_) * kBypassMixCoeff;
        const float mp = 1.f - bypass_mix_;
        out[0][i] = clipped_l * mp + raw_l * bypass_mix_;
        out[1][i] = clipped_r * mp + raw_r * bypass_mix_;

        // LED PWM. Higher PWM frequency means less perceptible flicker.
        const bool led_on = led_pwm_counter_ < led_duty;
        patch.SetLed(led_on);
        ++led_pwm_counter_;
        if (led_pwm_counter_ >= ott::kLedPwmPeriod) led_pwm_counter_ = 0;
    }

    // Sidechain CV: muted while bypassed, otherwise tracks Low band envelope.
    const float cv_v = bypassed
        ? 0.f
        : SidechainCv(bands[0].Envelope(), p.sidechain_threshold);
    patch.WriteCvOut(CV_OUT_1, cv_v);
}

int main(void) {
    patch.Init();

    const float sr = patch.AudioSampleRate();
    // All DSP defaults (crossover frequencies, gate timings, post EQ
    // frequencies and gain range, ducking smoothing) live in settings.h.
    gate.Init(sr);
    harmonic_gate.Init(sr);
    crossover.Init(sr);
    for (int b = 0; b < ott::Crossover::kNumBands; ++b)
        bands[b].Init(sr);
    post_eq.Init(sr);

    // One-pole coefficient for the ducking gain smoother:
    //   coeff = 1 - exp(-1 / (sr * tau))
    duck_coeff_ = 1.f - expf(-1.f / (sr * ott::settings::ducking::kSmoothS));

    controls.Init(patch);
    patch.SetLed(true);

    patch.StartAudio(AudioCallback);
    while (true) {}
}
