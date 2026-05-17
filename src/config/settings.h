#pragma once

// Centralised user-tunable defaults. Edit values here to change behaviour
// across the DSP modules without diving into each file. The DSP classes
// (Crossover, NoiseGate, PostEq) read these at Init() time.
//
// Anything not in this file is either a UX concern (bypass hold time, LED
// PWM) or an implementation invariant (RBJ formulas, bypass crossfade
// time). Move more knobs here if you find yourself wanting to tweak them.

namespace ott {
namespace settings {

// ---------------------------------------------------------------------------
// 4-band Linkwitz-Riley crossover frequencies (Hz)
// ---------------------------------------------------------------------------
// Three split points define the four bands.
//   bands: [Sub | Bass | Mid | High]
//   splits: kSubBassHz  = Sub / Bass
//           kBassMidHz  = Bass / Mid
//           kMidHighHz  = Mid / High
//
// Defaults below are biased for bass-heavy material: the sub band ends at
// 80 Hz, bass runs 80-300 Hz, mids span 300 Hz-3 kHz, treble above 3 kHz.
namespace crossover {
    static constexpr float kSubBassHz = 80.f;
    static constexpr float kBassMidHz = 300.f;
    static constexpr float kMidHighHz = 3000.f;
}

// ---------------------------------------------------------------------------
// Input noise gate
// ---------------------------------------------------------------------------
namespace gate {
    // Knob range for the open-threshold (knob 0..1 maps linearly in dB).
    static constexpr float kThreshMinDb = -80.f;
    static constexpr float kThreshMaxDb = -20.f;

    // Knob deadzone below which the gate is fully bypassed.
    static constexpr float kBypassDeadzone = 0.02f;

    // Hysteresis: gate closes at (open_threshold - kHysteresisDb).
    static constexpr float kHysteresisDb = 6.f;

    // Detector envelope follower (peak detection on max(|L|, |R|)).
    static constexpr float kEnvAttackS  = 0.001f; //  1 ms
    static constexpr float kEnvReleaseS = 0.030f; // 30 ms

    // Gain ramp (open / close transitions of the gate itself).
    static constexpr float kAttackS  = 0.002f;    //  2 ms
    static constexpr float kReleaseS = 0.100f;    // 100 ms

    // Hold time after the level falls below close threshold before the gate
    // actually starts closing. Avoids chattering on natural decays.
    static constexpr float kHoldS = 0.020f;       // 20 ms
}

// ---------------------------------------------------------------------------
// Per-band OTT processor (compressor with up + down action and makeup)
// ---------------------------------------------------------------------------
// Each band has its own envelope follower and gain computer; the values
// below are shared across all four bands. The knobs in the BANDS page
// (thresholds, makeups) and the time-multiplier knob (GLOBAL page 1) map
// into the ranges defined here.
namespace band {
    // Threshold knob 0..1 maps linearly in dB across this range. The same
    // threshold drives both downward (above) and upward (below) compression.
    static constexpr float kThreshMinDb = -60.f;
    static constexpr float kThreshMaxDb =   0.f;

    // Makeup gain knob 0..1 maps to 0..kMakeupMaxDb dB.
    static constexpr float kMakeupMaxDb = +24.f;

    // Compression ratio (applied symmetrically up and down). 3:1 matches
    // classic OTT character. Higher = more aggressive levelling.
    static constexpr float kRatio = 3.f;

    // Cap on how far below threshold the upward compressor responds, in
    // dB. With kRatio=3 and kBoostCapDb=24 the maximum upward boost is
    // ~16 dB; below that the gain holds rather than amplifying noise.
    static constexpr float kBoostCapDb = +24.f;

    // Nominal envelope follower time constants. The Time Multiplier knob
    // scales these by 2^(kTimeMultExpRange*(knob-0.5)) -> 0.25x..4x.
    static constexpr float kNominalAttackS   = 0.003f; //  3 ms
    static constexpr float kNominalReleaseS  = 0.080f; // 80 ms
    static constexpr float kTimeMultExpRange = 4.f;
}

// ---------------------------------------------------------------------------
// CV-input sidechain ducking
// ---------------------------------------------------------------------------
// One of the four CV jacks is repurposed as a sidechain input: positive CV
// on that jack ducks (attenuates) the processed audio output. The chosen
// jack is no longer summed into its corresponding band threshold.
//
//   CV at ~0 V (no patch) -> no ducking (deadzone)
//   CV positive            -> output gain scaled by (1 - cv*duck_depth)
//   CV negative            -> no ducking (treated as no signal)
//
// Bypass takes precedence: while the module is bypassed, the raw input
// passes through unaffected by ducking.
namespace ducking {
    // Which CV jack drives ducking. 0 = CV jack 5, 1 = CV6, 2 = CV7, 3 = CV8.
    // The chosen jack stops modulating its corresponding band threshold.
    static constexpr int kCvJackIndex = 0;

    // Smoothing time constant on the duck gain. ~5 ms removes ADC zipper
    // noise without sluggishly tracking real sidechain envelopes.
    static constexpr float kSmoothS = 0.005f;

    // Deadzone above 0 V centre (in normalised 0..1 ADC units, where 0.5 =
    // 0 V). CV values within this band of centre produce no ducking. Keeps
    // ADC offset / cable noise from causing a permanent slight duck when
    // nothing is patched.
    static constexpr float kDeadzone = 0.05f;
}

// ---------------------------------------------------------------------------
// 4-band post EQ
// ---------------------------------------------------------------------------
// One band per OTT crossover region (Sub / Bass / Mid / High). Outer bands
// are shelves anchored at the crossover corners; inner bands are bells
// centred (geometrically) inside their region:
//   Sub   low shelf  @ 80 Hz   (= Sub/Bass crossover corner)
//   Bass  peaking     @ 150 Hz  (centre of the 80-300 Hz band)
//   Mid   peaking     @ 950 Hz  (centre of the 300 Hz-3 kHz band)
//   High  high shelf  @ 3 kHz   (= Mid/High crossover corner)
namespace posteq {
    static constexpr float kSubFc       = 80.f;
    static constexpr float kBassFc      = 150.f;
    static constexpr float kBassQ       = 1.f;
    static constexpr float kMidFc       = 950.f;
    static constexpr float kMidQ        = 1.f;
    static constexpr float kHighFc      = 3000.f;
    static constexpr float kGainRangeDb = 12.f;   // +-kGainRangeDb dB at knob extremes
}

// ---------------------------------------------------------------------------
// Harmonic noise gate (per-notch static or dynamic)
// ---------------------------------------------------------------------------
// For every entry in kNotches[] the HarmonicGate runs a stereo biquad
// notch at (freq, Q) attenuating the tone. Per-entry sidechain_controlled
// chooses between two depth modes:
//   false (default) -- static: notch is always engaged at full strength
//                      scaled by the user knob. Cheapest. Right for
//                      stable mains hum / clock / SMPS whine where the
//                      noise level barely changes.
//   true            -- dynamic: a mono biquad bandpass at the same
//                      frequency (wider Q via kSidechainQRatio) feeds an
//                      envelope follower; the notch backs off as nearby
//                      content gets loud enough to mask the tone
//                      psychoacoustically. Use when a notch frequency
//                      might collide with musical content.
//
// Add/remove notches freely -- HarmonicGate is templated on the array
// size, so storage and the per-sample loop both follow this list
// automatically. CPU cost: ~28 cycles/sample/notch static, ~70 dynamic.
namespace notch {
    struct Spec {
        float freq;                        // Hz
        float Q;                           // higher = narrower notch
        bool  sidechain_controlled = false; // false = static (default)
    };

    // List of tonal noise frequencies. Edit freely; the rest follows.
    constexpr Spec kNotches[] = {
        // 60 Hz mains family
        {   59.21f, 12.f },
        {  118.40f, 20.f },
        {  178.90f, 20.f },
        {  238.20f, 20.f },
        {  298.00f, 20.f },
        // Digital clock ladder (powers of 2 + an orphan)
        {  255.70f, 20.f },
        {  390.20f, 20.f },
        {  511.40f, 20.f },
        { 1024.00f, 20.f },
        // SMPS-style whine
        { 6660.00f, 30.f },
        // Ultrasonic
        {17250.00f, 50.f },
    };
    constexpr int kNumNotches = sizeof(kNotches) / sizeof(kNotches[0]);

    // For entries with sidechain_controlled = true, the sidechain BPF Q
    // is set to notch_Q * this ratio. Lower ratio -> wider sidechain ->
    // adjacent musical content relaxes the notch more easily.
    // 0.2 -> sidechain_Q = notch_Q / 5.
    static constexpr float kSidechainQRatio = 0.2f;

    // Per-notch dynamic-depth curve, in sidechain envelope dBFS-ish:
    //   env <= kDepthLowDb  -> depth = 1 (notch engaged)
    //   env >= kDepthHighDb -> depth = 0 (notch bypassed; masked)
    //   linear ramp in between.
    static constexpr float kDepthLowDb  = -50.f;
    static constexpr float kDepthHighDb = -20.f;

    // Sidechain envelope follower.
    static constexpr float kEnvAttackS  = 0.050f; //  50 ms
    static constexpr float kEnvReleaseS = 0.200f; // 200 ms

    // Smoothing on the dynamic depth itself (separate from the env), to
    // keep the crossfade between dry/notched smooth on transitions.
    static constexpr float kDepthSmoothS = 0.100f; // 100 ms
}

} // namespace settings
} // namespace ott
