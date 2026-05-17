#pragma once

// Power-on default values for every knob-controlled parameter, expressed
// in normalised 0..1 "knob units" (the same domain GetAdcValue returns).
//
// Why this is separate from settings.h: settings.h holds static DSP
// constants (crossover splits, dB ranges, envelope time constants) that
// the DSP classes read once at Init() and that no knob ever changes. The
// values here are instead the *starting positions* of the knob-controlled
// parameters. With soft-takeover enabled, each one holds until its
// physical knob is moved into the pickup zone -- so this file defines the
// sound the module makes at boot, before anything is touched.
//
// Layout mirrors the UI pages in controls.h (see KnobContext()):
//   BANDS  page 0  thresholds   (Sub / Bass / Mid / High)
//   BANDS  page 1  makeups      (Sub / Bass / Mid / High)
//   BANDS  page 2  post EQ      (Sub / Bass / Mid / High)
//   GLOBAL page 0  depth / input_gain / output_gain / time_mult
//   GLOBAL page 1  gate / sidechain / duck / notch

namespace ott {
namespace defaults {

// ---------------------------------------------------------------------------
// BANDS pages (per-band, ordered Sub / Bass / Mid / High)
// ---------------------------------------------------------------------------
namespace bands {
    // page 0 -- compression thresholds.
    static constexpr float kThreshold[4] = { 0.35f, 0.60f, 0.66f, 0.60f };

    // page 1 -- makeup gain.
    static constexpr float kMakeup[4]    = { 0.57f, 0.66f, 0.66f, 0.66f };

    // page 2 -- post EQ (0.5 = unity gain per band).
    static constexpr float kPostEq[4]    = { 0.50f, 0.50f, 0.50f, 0.50f };
}

// ---------------------------------------------------------------------------
// GLOBAL pages
// ---------------------------------------------------------------------------
namespace global {
    // page 0.
    static constexpr float kDepth      = 0.00f; // fully dry
    static constexpr float kInputGain  = 0.50f; // unity (0.5 = 0 dB)
    static constexpr float kOutputGain = 0.50f; // unity
    static constexpr float kTimeMult   = 0.25f; // 25%

    // page 1.
    static constexpr float kGateThreshold      = 0.75f; // 75%
    static constexpr float kSidechainThreshold = 0.60f; // 60% (sidechain CV
                                                        // live at boot)
    static constexpr float kDuckDepth          = 1.00f; // full ducking w/ CV
    static constexpr float kNotchDepth         = 1.00f; // harmonic gate full
}

} // namespace defaults
} // namespace ott
