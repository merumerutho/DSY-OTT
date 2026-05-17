#pragma once

#include "daisy_patch_sm.h"
#include "hid/switch.h"
#include "settings.h"

namespace ott {

// Pin map (from Electrosmith patch.Init() v1.0 databrief, MAR/13/2025).
//   Knobs 1..4         -> Patch SM CV_1..CV_4   (ADC indices 0..3)
//   CV jacks 5..8      -> Patch SM CV_5..CV_8   (ADC indices 4..7)
//   Button (B7)        -> daisy::patch_sm::DaisyPatchSM::B7
//   Toggle (B8, 2-pos) -> daisy::patch_sm::DaisyPatchSM::B8
//   CV out 1 (C10)     -> daisy::patch_sm::CV_OUT_1
struct PinMap {
    static constexpr int KNOB_1 = daisy::patch_sm::CV_1;
    static constexpr int KNOB_2 = daisy::patch_sm::CV_2;
    static constexpr int KNOB_3 = daisy::patch_sm::CV_3;
    static constexpr int KNOB_4 = daisy::patch_sm::CV_4;
    static constexpr int CV_IN_1 = daisy::patch_sm::CV_5;
    static constexpr int CV_IN_2 = daisy::patch_sm::CV_6;
    static constexpr int CV_IN_3 = daisy::patch_sm::CV_7;
    static constexpr int CV_IN_4 = daisy::patch_sm::CV_8;
};

enum class Page : uint8_t { Bands = 0, Global = 1 };

// Long-press threshold for bypass. Long enough that pressing-and-releasing
// for "tap to advance page" never accidentally triggers bypass.
static constexpr float kBypassHoldMs = 1500.f;

// LED PWM resolution. The audio callback ticks led_pwm_counter_ once per
// sample and turns the LED on while counter < duty. At 48 kHz / 96-sample
// period this is a 500 Hz PWM, well above flicker threshold.
static constexpr uint32_t kLedPwmPeriod = 96;
static constexpr uint32_t kLedDutyBright = kLedPwmPeriod;     // ~100% duty
static constexpr uint32_t kLedDutyDim    = kLedPwmPeriod / 8; // ~12% duty

// Number of selectable sub-pages within each toggle group.
static constexpr uint8_t kNumPagesPerToggle = 2;

// Holds the resolved parameter values seen by the DSP layer. All values 0..1
// unless noted; the DSP layer scales/maps them to physical units.
struct Params {
    // Per-band parameters (Low, LoMid, MidHigh, High).
    float threshold[4];
    float makeup[4];

    // Global parameters.
    float depth;          // 0 = dry, 1 = full wet
    float input_gain;     // 0..1, -24..+24 dB linear-in-dB
    float output_gain;    // 0..1, -24..+24 dB linear-in-dB
    float notch_depth;    // 0..1, global scale on the harmonic notch gate

    // Noise gate (lives at the input). 0 = bypass, otherwise 0..1 maps to
    // open-threshold range (-80..-20 dB). Hardcoded attack/release/hold.
    float gate_threshold;

    // Sidechain CV (CV_OUT_1) derived from the Low band envelope. 0 = mute
    // CV output entirely; otherwise 0..1 maps to threshold (-60..0 dBFS).
    // Above the threshold the CV climbs from 0..5V over a 12 dB range.
    float sidechain_threshold;

    // 3-band post EQ. Each 0..1 -> -12..+12 dB linear-in-dB (0.5 = unity).
    float post_eq_low;
    float post_eq_mid;
    float post_eq_high;

    // CV-input ducking depth. 0 = no ducking even with CV, 1 = full ducking.
    // The actual ducking amount is duck_depth * (positive CV signal).
    float duck_depth;

    // CV jack values. The configured ducking jack drives ducking; the others
    // are summed into the corresponding band thresholds.
    float cv_in[4];
};

// Page layout (LED brightness shown):
//
//   Toggle UP (BANDS):
//     page 0 [bright]  no shift: 4 thresholds
//                         shift: 4 makeups
//     page 1 [dim]     no shift: post EQ low / mid / high / -
//
//   Toggle DOWN (GLOBAL):
//     page 0 [bright]  no shift: depth / in_gain / out_gain / notch_depth
//                         shift: gate_threshold / sidechain_threshold / - / -
//     page 1 [dim]     no shift: (reserved for future)
//
// Tap (press + release before kBypassHoldMs) advances the current toggle's
// page index. Each toggle position remembers its own page, so flipping
// the toggle does not lose where you were.
class Controls {
  public:
    void Init(daisy::patch_sm::DaisyPatchSM& patch) {
        button_.Init(patch.B7);
        toggle_.Init(patch.B8,
                     0.f,
                     daisy::Switch::TYPE_TOGGLE,
                     daisy::Switch::POLARITY_INVERTED,
                     daisy::GPIO::Pull::PULLUP);

        for (int i = 0; i < 4; ++i) {
            params_.threshold[i] = 0.5f;
            params_.makeup[i]    = 0.5f;
            params_.cv_in[i]     = 0.f;
        }
        params_.depth               = 1.0f;
        params_.input_gain          = 0.5f;
        params_.output_gain         = 0.5f;
        params_.notch_depth         = 0.f;  // bypassed
        params_.gate_threshold      = 0.f;  // bypassed
        params_.sidechain_threshold = 0.f;  // muted
        params_.post_eq_low         = 0.5f; // unity
        params_.post_eq_mid         = 0.5f;
        params_.post_eq_high        = 0.5f;
        params_.duck_depth          = 1.0f; // full ducking when CV is present
    }

    // Call once per audio block.
    void Process(daisy::patch_sm::DaisyPatchSM& patch) {
        button_.Debounce();
        toggle_.Debounce();

        // Long-press = bypass. Consume so we toggle exactly once per press.
        if (button_.TimeHeldMs() > kBypassHoldMs && !long_press_consumed_) {
            bypass_              = !bypass_;
            long_press_consumed_ = true;
        }

        // Tap = advance page. A "tap" is any release that did not already
        // long-press into the bypass action.
        if (button_.FallingEdge()) {
            if (!long_press_consumed_) {
                if (toggle_.Pressed())
                    page_idx_global_ = (page_idx_global_ + 1) % kNumPagesPerToggle;
                else
                    page_idx_bands_  = (page_idx_bands_  + 1) % kNumPagesPerToggle;
            }
            long_press_consumed_ = false;
        }

        // Shift only counts as "shift" while not crossing into bypass-territory.
        const bool shift = button_.Pressed() && !long_press_consumed_;
        const Page page  = toggle_.Pressed() ? Page::Global : Page::Bands;
        const uint8_t page_idx =
            (page == Page::Global) ? page_idx_global_ : page_idx_bands_;

        const float k[4] = {
            patch.GetAdcValue(PinMap::KNOB_1),
            patch.GetAdcValue(PinMap::KNOB_2),
            patch.GetAdcValue(PinMap::KNOB_3),
            patch.GetAdcValue(PinMap::KNOB_4),
        };
        const float cv[4] = {
            patch.GetAdcValue(PinMap::CV_IN_1),
            patch.GetAdcValue(PinMap::CV_IN_2),
            patch.GetAdcValue(PinMap::CV_IN_3),
            patch.GetAdcValue(PinMap::CV_IN_4),
        };
        for (int i = 0; i < 4; ++i)
            params_.cv_in[i] = cv[i];

        // CV jacks offset the four band thresholds (bipolar on SM, centred
        // at 0.5 = no offset). The jack reserved for ducking is excluded;
        // its value is used by the audio callback for sidechain ducking.
        float thr_cv_offset[4];
        for (int i = 0; i < 4; ++i) {
            thr_cv_offset[i] = (i == settings::ducking::kCvJackIndex)
                ? 0.f
                : (cv[i] - 0.5f);
        }

        if (page == Page::Bands) {
            if (page_idx == 0) {
                if (shift) {
                    for (int i = 0; i < 4; ++i)
                        params_.makeup[i] = k[i];
                } else {
                    for (int i = 0; i < 4; ++i)
                        params_.threshold[i] = clamp01(k[i] + thr_cv_offset[i]);
                }
            } else { // page 1 = post EQ
                params_.post_eq_low  = k[0];
                params_.post_eq_mid  = k[1];
                params_.post_eq_high = k[2];
                // K4 reserved
            }
        } else { // Page::Global
            if (page_idx == 0) {
                params_.depth       = k[0];
                params_.input_gain  = k[1];
                params_.output_gain = k[2];
                params_.notch_depth = k[3];
            } else { // page 1 = gate / sidechain / duck
                params_.gate_threshold      = k[0];
                params_.sidechain_threshold = k[1];
                params_.duck_depth          = k[2];
                // K4 reserved
            }
        }

        page_     = page;
        page_idx_ = page_idx;
        shift_    = shift;
    }

    const Params& params() const { return params_; }
    Page          page() const { return page_; }
    uint8_t       page_idx() const { return page_idx_; }
    bool          shift() const { return shift_; }
    bool          Bypassed() const { return bypass_; }

    // Returns the LED PWM duty in [0, kLedPwmPeriod] for the current state.
    // While bypassed the duty alternates between the page brightness and 0
    // at 1 Hz, giving a "flashing at the current page brightness" cue.
    uint32_t LedDuty(uint32_t now_ms) const {
        const uint32_t base =
            (page_idx_ == 0) ? kLedDutyBright : kLedDutyDim;
        if (bypass_) {
            const bool on_phase = (now_ms % 1000) < 500;
            return on_phase ? base : 0;
        }
        return base;
    }

  private:
    static float clamp01(float v) {
        return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    }

    daisy::Switch button_;
    daisy::Switch toggle_;
    Params        params_{};
    Page          page_                = Page::Bands;
    uint8_t       page_idx_            = 0;
    uint8_t       page_idx_bands_      = 0;
    uint8_t       page_idx_global_     = 0;
    bool          shift_               = false;
    bool          bypass_              = false;
    bool          long_press_consumed_ = false;
};

} // namespace ott
