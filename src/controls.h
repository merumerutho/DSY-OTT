#pragma once

#include "daisy_patch_sm.h"
#include "hid/switch.h"
#include "PagedControls.hpp"  // grid storage + soft-takeover pickup + persistence
#include "settings.h"
#include "defaults.h"
#include <string.h>  // memcpy / memcmp for the saved-state snapshot

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

// Hold time that triggers "save all pages to flash". Long enough that
// pressing-and-releasing for "tap to advance page" never accidentally
// triggers a save.
static constexpr float kSaveHoldMs = 1500.f;

// Save-acknowledge LED flash: a rapid but observable burst lasting under
// one second. At the 1000 Hz block rate, kSaveFlashTotal blocks total,
// toggling every kSaveFlashHalf blocks (~16.7 Hz, ~13 visible flashes).
static constexpr uint32_t kSaveFlashTotal = 800; // ~0.8 s
static constexpr uint32_t kSaveFlashHalf  = 30;  // ~16.7 Hz toggle

// LED blink periods in audio blocks (at 48 kHz / 48-sample blocks = 1000 Hz
// block rate). Page 0 = solid on. Page 1 = fast blink (~4 Hz, 250 blocks
// half-period). Page 2 = slow blink (~1.5 Hz, 333 blocks half-period).
static constexpr uint32_t kLedBlinkFastHalf = 125;  // ~4 Hz
static constexpr uint32_t kLedBlinkSlowHalf = 333;  // ~1.5 Hz

// Number of selectable sub-pages within each toggle group.
static constexpr uint8_t kNumPagesBands  = 3;
static constexpr uint8_t kNumPagesGlobal = 2;

// Block counter wrap for LED blink timing. LCM of the two blink periods
// (250 and 666 blocks) so both patterns stay phase-aligned across wraps.
// At 1000 Hz block rate this wraps every ~83 s — no visible glitch.
static constexpr uint32_t kBlinkWrapBlocks = 83250;

// Soft-takeover: physical knob must come within this distance of the
// stored parameter value before it starts tracking. ~3% of full travel.
static constexpr float kPickupThreshold = 0.03f;

// Number of distinct knob-to-parameter mappings (toggle * page).
//   0 = BANDS page 0   (thresholds)
//   1 = BANDS page 1   (makeups)
//   2 = BANDS page 2   (post EQ)
//   3 = GLOBAL page 0  (depth / gains / time_mult)
//   4 = GLOBAL page 1  (gate / sidechain / duck)
static constexpr int kNumKnobContexts = 5;

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
    float time_mult;      // 0..1, scales attack/release times

    // Noise gate (lives at the input). 0 = bypass, otherwise 0..1 maps to
    // open-threshold range (-80..-20 dB). Hardcoded attack/release/hold.
    float gate_threshold;

    // Sidechain CV (CV_OUT_1) derived from the Low band envelope. 0 = mute
    // CV output entirely; otherwise 0..1 maps to threshold (-60..0 dBFS).
    // Above the threshold the CV climbs from 0..5V over a 12 dB range.
    float sidechain_threshold;

    // 4-band post EQ (Sub / Bass / Mid / High). Each 0..1 -> -12..+12 dB
    // linear-in-dB (0.5 = unity).
    float post_eq_sub;
    float post_eq_bass;
    float post_eq_mid;
    float post_eq_high;

    // CV-input ducking depth. 0 = no ducking even with CV, 1 = full ducking.
    // The actual ducking amount is duck_depth * (positive CV signal).
    float duck_depth;

    // Global scale on the harmonic notch gate. 0 = bypassed, 1 = each
    // notch can reach full strength when its sidechain (if any) allows.
    float notch_depth;

    // CV jack values. The configured ducking jack drives ducking; the others
    // are summed into the corresponding band thresholds.
    float cv_in[4];
};

// Page layout (LED pattern shown):
//
//   Toggle UP (BANDS):
//     page 0 [solid]       4 thresholds       (Sub / Bass / Mid / High)
//     page 1 [fast blink]  4 makeups          (Sub / Bass / Mid / High)
//     page 2 [slow blink]  4 post EQ gains    (Sub / Bass / Mid / High)
//
//   Toggle DOWN (GLOBAL):
//     page 0 [solid]       depth / in_gain / out_gain / time_mult
//     page 1 [fast blink]  gate_threshold / sidechain_threshold / duck_depth / notch_depth
//
// Tap (press + release before kSaveHoldMs) advances the current toggle's
// page index. Each toggle position remembers its own page, so flipping
// the toggle does not lose where you were. Holding past kSaveHoldMs
// instead saves every page's values to flash (LED flashes rapidly).
class Controls {
  public:
    // Persisted snapshot of every page's knob values (the whole grid, all
    // pages at once -- never individual values or pages). Saved to / loaded
    // from QSPI flash via daisy::PersistentStorage in main(). The magic +
    // version words let startup tell a real saved snapshot apart from a
    // blank or schema-mismatched chip; on mismatch the compiled defaults
    // are kept.
    static constexpr uint32_t kStoreMagic   = 0x4F545453u; // 'OTTS'
    static constexpr uint32_t kStoreVersion = 1u;

    struct StoredState {
        uint32_t magic;
        uint32_t version;
        float    knob_slot[kNumKnobContexts][4];

        bool operator==(const StoredState& o) const {
            return memcmp(this, &o, sizeof(*this)) == 0;
        }
        bool operator!=(const StoredState& o) const { return !(*this == o); }
    };

    void Init(daisy::patch_sm::DaisyPatchSM& patch) {
        button_.Init(patch.B7);
        toggle_.Init(patch.B8,
                     0.f,
                     daisy::Switch::TYPE_TOGGLE,
                     daisy::Switch::POLARITY_INVERTED,
                     daisy::GPIO::Pull::PULLUP);

        // All power-on values live in defaults.h (normalised knob units).
        // Seed the whole knob grid from them, then derive params_ so the two
        // can never drift. main() may overwrite this with a saved snapshot
        // via LoadState() if the flash holds valid data.
        //   Grid rows mirror the UI pages (see KnobContext()):
        //   [0] thresholds  [1] makeups  [2] post EQ
        //   [3] depth / in_gain / out_gain / time_mult
        //   [4] gate / sidechain / duck / notch
        float seed[kNumKnobContexts][4];
        for (int i = 0; i < 4; ++i) {
            seed[0][i] = defaults::bands::kThreshold[i];
            seed[1][i] = defaults::bands::kMakeup[i];
            seed[2][i] = defaults::bands::kPostEq[i];
            params_.cv_in[i] = 0.f;
        }
        seed[3][0] = defaults::global::kDepth;
        seed[3][1] = defaults::global::kInputGain;
        seed[3][2] = defaults::global::kOutputGain;
        seed[3][3] = defaults::global::kTimeMult;
        seed[4][0] = defaults::global::kGateThreshold;
        seed[4][1] = defaults::global::kSidechainThreshold;
        seed[4][2] = defaults::global::kDuckDepth;
        seed[4][3] = defaults::global::kNotchDepth;

        // PagedControls owns the grid + soft-takeover pickup. We drive page
        // selection ourselves via GoToPage() (the toggle picks one of two
        // page groups, so navigation is not the library's linear cycling),
        // and we keep our own LED scheme -- so the LED timing passed here is
        // unused. RecomputeAllParams() derives params_ from the seeded grid.
        pg_.Init(seed, kPickupThreshold);
        RecomputeAllParams();

        toggle_.Debounce();
        const Page boot_page = toggle_.Pressed() ? Page::Global : Page::Bands;
        // Position the grid on the boot context. GoToPage() leaves every knob
        // un-picked-up, so the seeded defaults hold until each physical knob
        // is moved into the pickup zone (kPickupThreshold). The ADC is NOT
        // latched at startup -- the boot context behaves like any other.
        pg_.GoToPage(KnobContext(boot_page, 0));
    }

    // Call once per audio block.
    void Process(daisy::patch_sm::DaisyPatchSM& patch) {
        button_.Debounce();
        toggle_.Debounce();

        // Hold >= kSaveHoldMs = snapshot every page and request a flash
        // save. Consumed so it fires exactly once per press. The actual
        // QSPI write happens in the main loop (never in this audio ISR);
        // here we only capture a consistent snapshot and start the LED ack.
        if (button_.TimeHeldMs() > kSaveHoldMs && !long_press_consumed_) {
            pg_.SaveGrid(&save_snapshot_[0][0]);
            save_pending_        = true;
            save_flash_left_     = kSaveFlashTotal;
            long_press_consumed_ = true;
        }

        // Tap = advance page. A "tap" is any release that did not already
        // long-press into the save action.
        if (button_.FallingEdge()) {
            if (!long_press_consumed_) {
                if (toggle_.Pressed())
                    page_idx_global_ = (page_idx_global_ + 1) % kNumPagesGlobal;
                else
                    page_idx_bands_  = (page_idx_bands_  + 1) % kNumPagesBands;
            }
            long_press_consumed_ = false;
        }

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

        // Select the active grid page from toggle + per-group page index.
        // GoToPage() resets pickup (knobs must be re-caught on the new page),
        // exactly the old per-context soft-takeover reset; it only fires when
        // the context actually changes, so pickup persists otherwise. Then
        // Process() runs soft-takeover for this block, updating the active
        // page's stored values for any knob that has been picked up.
        const uint8_t knob_ctx = KnobContext(page, page_idx);
        if (knob_ctx != pg_.Page())
            pg_.GoToPage(knob_ctx);
        pg_.Process(k, false);

        // Map the active page's (post-pickup) values into params_. Only the
        // displayed page is recomputed each block -- inactive pages keep the
        // values they last resolved to (e.g. CV offset folds into thresholds
        // only while the thresholds page is shown).
        if (page == Page::Bands) {
            if (page_idx == 0) { // thresholds
                for (int i = 0; i < 4; ++i)
                    params_.threshold[i] = clamp01(pg_.Value(0, i)
                                                   + thr_cv_offset[i]);
            } else if (page_idx == 1) { // makeups
                for (int i = 0; i < 4; ++i)
                    params_.makeup[i] = pg_.Value(1, i);
            } else { // page 2 = post EQ (Sub / Bass / Mid / High)
                params_.post_eq_sub  = pg_.Value(2, 0);
                params_.post_eq_bass = pg_.Value(2, 1);
                params_.post_eq_mid  = pg_.Value(2, 2);
                params_.post_eq_high = pg_.Value(2, 3);
            }
        } else { // Page::Global
            if (page_idx == 0) {
                params_.depth       = pg_.Value(3, 0);
                params_.input_gain  = pg_.Value(3, 1);
                params_.output_gain = pg_.Value(3, 2);
                params_.time_mult   = pg_.Value(3, 3);
            } else { // page 1 = gate / sidechain / duck / notch
                params_.gate_threshold      = pg_.Value(4, 0);
                params_.sidechain_threshold = pg_.Value(4, 1);
                params_.duck_depth          = pg_.Value(4, 2);
                params_.notch_depth         = pg_.Value(4, 3);
            }
        }

        page_     = page;
        page_idx_ = page_idx;

        if (save_flash_left_ > 0) --save_flash_left_;
        if (++blink_counter_ >= kBlinkWrapBlocks) blink_counter_ = 0;
    }

    const Params& params() const { return params_; }
    Page          page() const { return page_; }
    uint8_t       page_idx() const { return page_idx_; }

    // Called from the main loop. If the hold gesture requested a save,
    // fills `out` with the magic-tagged snapshot and returns true (once);
    // the caller then persists `out` via PersistentStorage::Save(). The
    // snapshot was taken atomically in Process() (audio ISR) so no value
    // can tear across the read here.
    bool ConsumeSaveRequest(StoredState& out) {
        if (!save_pending_) return false;
        out.magic   = kStoreMagic;
        out.version = kStoreVersion;
        memcpy(out.knob_slot, save_snapshot_, sizeof(save_snapshot_));
        save_pending_ = false;
        return true;
    }

    // Called once at startup if the flash holds a valid snapshot. Replaces
    // the compiled defaults with the saved grid for every page at once.
    void LoadState(const StoredState& s) {
        pg_.LoadGrid(&s.knob_slot[0][0]);
        RecomputeAllParams();
    }

    // Returns whether the LED should be on this block.
    //   save ack : rapid ~16.7 Hz flash for <1 s (overrides everything).
    //   page 0   : solid on.
    //   page 1   : fast blink (~4 Hz).
    //   page 2   : slow blink (~1.5 Hz).
    bool LedOn() const {
        if (save_flash_left_ > 0) {
            const uint32_t elapsed = kSaveFlashTotal - save_flash_left_;
            return ((elapsed / kSaveFlashHalf) & 1u) == 0u;
        }
        if (page_idx_ == 0)
            return true;
        if (page_idx_ == 1)
            return (blink_counter_ % (kLedBlinkFastHalf * 2)) < kLedBlinkFastHalf;
        return (blink_counter_ % (kLedBlinkSlowHalf * 2)) < kLedBlinkSlowHalf;
    }

  private:
    // Map the entire knob grid into params_ (all pages at once). Used by
    // Init() (defaults) and LoadState() (saved snapshot) so both paths
    // produce identical params_/grid coupling. Threshold takes no CV offset
    // here; Process() adds the live CV per block.
    void RecomputeAllParams() {
        for (int i = 0; i < 4; ++i) {
            params_.threshold[i] = pg_.Value(0, i);
            params_.makeup[i]    = pg_.Value(1, i);
        }
        params_.post_eq_sub  = pg_.Value(2, 0);
        params_.post_eq_bass = pg_.Value(2, 1);
        params_.post_eq_mid  = pg_.Value(2, 2);
        params_.post_eq_high = pg_.Value(2, 3);
        params_.depth        = pg_.Value(3, 0);
        params_.input_gain   = pg_.Value(3, 1);
        params_.output_gain  = pg_.Value(3, 2);
        params_.time_mult    = pg_.Value(3, 3);
        params_.gate_threshold      = pg_.Value(4, 0);
        params_.sidechain_threshold = pg_.Value(4, 1);
        params_.duck_depth          = pg_.Value(4, 2);
        params_.notch_depth         = pg_.Value(4, 3);
    }

    static float clamp01(float v) {
        return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    }

    static uint8_t KnobContext(Page pg, uint8_t pidx) {
        if (pg == Page::Bands) return pidx;  // 0, 1, 2
        return 3 + pidx;                     // 3, 4
    }

    daisy::Switch button_;
    daisy::Switch toggle_;
    Params        params_{};
    Page          page_                = Page::Bands;
    uint8_t       page_idx_            = 0;
    uint8_t       page_idx_bands_      = 0;
    uint8_t       page_idx_global_     = 0;
    bool          long_press_consumed_ = false;
    volatile bool save_pending_        = false;  // ISR sets, main clears
    uint32_t      save_flash_left_     = 0;       // ISR-only LED ack countdown
    uint32_t      blink_counter_       = 0;
    float         save_snapshot_[kNumKnobContexts][4] = {};

    // Grid storage (kNumKnobContexts pages x 4 knobs) + soft-takeover pickup
    // + flash persistence. Page navigation (toggle + per-group index) and the
    // LED scheme stay above; we drive page selection via GoToPage().
    pagedctl::PagedControls<kNumKnobContexts, 4> pg_;
};

} // namespace ott
