// OTT-style multiband effect for the Electrosmith patch.Init() module.
//
// Signal chain:
//   in -> input_gain -> harmonic_gate -> noise_gate -> (split 4 bands)
//      -> per-band up/down -> sum -> [depth crossfade vs post-gate dry]
//      -> output_gain -> post_eq (4-band) -> cv_duck -> soft_clip -> out
//
// The harmonic gate removes stable tonal noise (hum, clock leakage,
// SMPS whine) before the broadband noise gate so the gate's threshold
// detection sees a cleaner residual floor.
//
// CV out 1 = sidechain output from Low band envelope.
// CV out 2 = user LED (carrier board pin C1). 5 V = on, 0 V = off.
// CV in (configured jack) = sidechain ducking input.

#include "daisy_patch_sm.h"
#include "sys/system.h"
#include "util/PersistentStorage.h"
#include "controls.h"
#include "gate.h"
#include "harmonic_gate.h"
#include "crossover.h"
#include "ott_band.h"
#include "posteq.h"
#include "settings.h"
#include "fast_math.h"
#include "oversample.h"
#include "profile.h"
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

// 2x oversampled output soft-clip (per channel). Tames the aliasing the
// clipper would otherwise fold into the band -- a big win on harmonically
// dense Game Boy material. On by default; build with
// -DOTT_NO_CLIP_OVERSAMPLE to A/B against the plain per-sample clipper.
#ifndef OTT_NO_CLIP_OVERSAMPLE
static constexpr int kClipOsTaps = 64;
static ott::Oversampler<2, kClipOsTaps> os_l;
static ott::Oversampler<2, kClipOsTaps> os_r;
#endif

// Persistent storage for all page settings on the onboard QSPI flash.
// Holds one StoredState (every page's knob grid + magic/version). Save()
// is blocking flash I/O -- only ever called from the main loop.
static daisy::PersistentStorage<ott::Controls::StoredState> storage(patch.qspi);

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

// over / denom, with the single divide swappable for the bit-trick
// reciprocal. denom is always >= kRange (0.1) and positive here, so we
// use FastInvAbs (no sign handling) and its 0 / Inf / NaN / subnormal
// exclusions never apply. OTT_FAST_SOFTCLIP trades the ~14-cycle
// non-pipelined hardware VDIV.F32 for ~2.5e-3 relative error -- only
// marginally cheaper on this M7, so A/B it with the profiler before
// keeping it. Flag OFF (default) is byte-identical to a plain division.
static inline float ClipRatio(float over, float denom) {
#ifdef OTT_FAST_SOFTCLIP
    return over * ott::FastInvAbs(denom);  // denom >= 0.1 > 0
#else
    return over / denom;
#endif
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
        return kKnee + kRange * ClipRatio(over, over + kRange);
    }
    if (x < -kKnee) {
        const float over = -x - kKnee;
        return -(kKnee + kRange * ClipRatio(over, over + kRange));
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
    OTT_PROFILE_BEGIN();

    patch.ProcessAllControls();
    controls.Process(patch);

    const ott::Params& p = controls.params();

    gate.SetThreshold(p.gate_threshold);
    harmonic_gate.SetDepth(p.notch_depth);
    for (int b = 0; b < ott::Crossover::kNumBands; ++b) {
        bands[b].SetThreshold(p.threshold[b]);
        bands[b].SetMakeup(p.makeup[b]);
        bands[b].SetTimeMult(p.time_mult);
    }
    post_eq.SetGainsNorm(p.post_eq_sub, p.post_eq_bass,
                         p.post_eq_mid, p.post_eq_high);

    const float in_gain         = NormToGainLinear(p.input_gain);
    const float out_gain        = NormToGainLinear(p.output_gain);
    const float depth           = p.depth;
    const float dry_amt         = 1.f - depth;
    const bool  led_on          = controls.LedOn();

    // Resolve ducking target gain for this block. Reads the configured CV
    // jack, takes the positive portion above a small deadzone, scales by
    // the depth knob, and converts into a target gain in [1-depth, 1].
    const float cv_raw = p.cv_in[ott::settings::ducking::kCvJackIndex];
    float cv_signal = (cv_raw - 0.5f) * 2.f; // -1..+1 (0V centred)
    if (cv_signal < ott::settings::ducking::kDeadzone) cv_signal = 0.f;
    if (cv_signal > 1.f) cv_signal = 1.f;
    const float duck_amount = cv_signal * p.duck_depth; // 0..1
    const float duck_target = 1.f - duck_amount;

    for (size_t i = 0; i < size; ++i) {
        float l = in[0][i] * in_gain;
        float r = in[1][i] * in_gain;

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

        // CV-input ducking: smooth toward target gain and apply.
        duck_gain_ += (duck_target - duck_gain_) * duck_coeff_;
        mix_l *= duck_gain_;
        mix_r *= duck_gain_;

#ifdef OTT_NO_CLIP_OVERSAMPLE
        out[0][i] = SoftClip(mix_l);
        out[1][i] = SoftClip(mix_r);
#else
        out[0][i] = os_l.Process(mix_l, [](float v) { return SoftClip(v); });
        out[1][i] = os_r.Process(mix_r, [](float v) { return SoftClip(v); });
#endif
    }

    // LED on CV_OUT_2 (carrier board pin C1). 5V = on, 0V = off.
    patch.WriteCvOut(CV_OUT_2, led_on ? 5.f : 0.f);

    // Sidechain CV on CV_OUT_1.
    const float cv_v = SidechainCv(bands[0].Envelope(), p.sidechain_threshold);
    patch.WriteCvOut(CV_OUT_1, cv_v);

    OTT_PROFILE_END();
}

#ifdef OTT_PROFILE_ENABLED
static void ProfilePrintLine(const char* s) { patch.PrintLine("%s", s); }
static uint32_t ProfileNowMs()              { return System::GetNow(); }
#endif

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
#ifndef OTT_NO_CLIP_OVERSAMPLE
    os_l.Init();
    os_r.Init();
#endif

    // One-pole coefficient for the ducking gain smoother:
    //   coeff = 1 - exp(-1 / (sr * tau))
    duck_coeff_ = 1.f - expf(-1.f / (sr * ott::settings::ducking::kSmoothS));

    controls.Init(patch);

    // Load saved page settings from QSPI if the chip holds a valid,
    // matching snapshot; otherwise keep the compiled defaults. The
    // zero-magic "factory" struct means a blank or foreign chip never
    // masquerades as user data (PersistentStorage stores this on first
    // ever boot; its magic stays 0 until the user actually saves).
    ott::Controls::StoredState defs{}; // magic/version = 0 -> invalid
    storage.Init(defs);
    {
        const ott::Controls::StoredState& s = storage.GetSettings();
        if (s.magic == ott::Controls::kStoreMagic
            && s.version == ott::Controls::kStoreVersion)
            controls.LoadState(s);
    }

    OTT_PROFILE_INIT(System::GetSysClkFreq(),
                     patch.AudioBlockSize(),
                     (uint32_t)sr);
#ifdef OTT_PROFILE_ENABLED
    patch.StartLog(false);
#endif

    patch.StartAudio(AudioCallback);
    while (true) {
        // Drain a pending save request from the hold gesture. The blocking
        // QSPI erase/write lives here in thread mode, never in the audio
        // ISR. Save() only actually writes if the snapshot differs from
        // what is already stored (StoredState::operator!=).
        if (controls.ConsumeSaveRequest(storage.GetSettings()))
            storage.Save();

        OTT_PROFILE_POLL(ProfilePrintLine, ProfileNowMs);
    }
}
