#pragma once

#include <math.h>

namespace ott {

// L-times polyphase oversampler for a memoryless nonlinearity.
//
// Per base-rate input sample it:
//   1. interpolates x to L samples (zero-stuff + anti-imaging FIR, applied
//      in polyphase form so the stuffed zeros cost nothing),
//   2. runs the supplied shaper on each of the L,
//   3. low-pass filters and decimates back to 1 (anti-aliasing FIR).
//
// Both filters use the SAME linear-phase prototype: a Blackman-windowed
// sinc low-pass. Coefficients are computed once in Init() from the closed
// form below -- there are no hand-entered tap tables to mistype, so the
// filter is correct by construction and easy to retune (kTaps).
//
// Note on cost vs L: at 2x the filter's transition band, relative to the
// oversampled Nyquist, is much wider than at 4x, so far fewer taps reach
// the same image/alias rejection. 2x is therefore a lot cheaper than
// "half of 4x" for comparable quality. It is still a moderate-Q filter,
// not a brickwall: content very close to base Nyquist is only partly
// rejected -- the deliberate clipper-oversampling tradeoff. The big win is
// killing the strong low/mid alias products that fold deep into the band.
//
// Group delay: ~(kTaps-1)/L base samples. Irrelevant for an insert effect.
// Cost: ~1.5*kTaps multiply-adds per input sample per channel: kTaps for
// interpolation plus ~kTaps/2 for decimation (symmetric taps are folded).
template <int L, int kTaps>
class Oversampler {
    static_assert(kTaps % L == 0, "kTaps must be a multiple of L");
    static constexpr int P = kTaps / L;  // polyphase branch length

  public:
    // Anti-imaging / anti-aliasing cutoff, normalised to the OVERSAMPLED
    // rate (cycles/sample at L*Fs): pass ~0.45*Fs, i.e. 0.45/L, then roll
    // off below the first image.
    static constexpr double kCutoff = 0.45 / (double)L;

    void Init() {
        constexpr double kPi = 3.14159265358979323846;
        const double     mid = (kTaps - 1) / 2.0;

        double sum = 0.0;
        double h[kTaps];
        for (int k = 0; k < kTaps; ++k) {
            const double d = k - mid;
            const double sinc = (d == 0.0)
                ? 2.0 * kCutoff
                : sin(2.0 * kPi * kCutoff * d) / (kPi * d);
            const double w = 0.42
                - 0.50 * cos(2.0 * kPi * k / (kTaps - 1))
                + 0.08 * cos(4.0 * kPi * k / (kTaps - 1));
            h[k] = sinc * w;
            sum += h[k];
        }
        // Normalise to unity DC gain; interpolation is scaled by L to
        // compensate the 1/L energy lost to zero-stuffing.
        for (int k = 0; k < kTaps; ++k) {
            const float hk = (float)(h[k] / sum);
            up_h_[k] = (float)L * hk;
            dn_h_[k] = hk;
        }

        for (int j = 0; j < 2 * P; ++j) xb_[j] = 0.f;
        for (int m = 0; m < 2 * kTaps; ++m) ub_[m] = 0.f;
        xr_ = 0;
        ur_ = 0;
    }

    // One base-rate sample in, one filtered, oversampled-shaped sample out.
    // `shape` is any float(float) callable (pass a lambda so it inlines).
    template <typename Shaper>
    float Process(float x, Shaper shape) {
        // Push x into a linear double-length history so the polyphase
        // interpolation reads a contiguous P-sample window. Mirroring the
        // write into the upper half makes every length-P window valid with
        // no wraparound -- the old O(P) per-sample memmove is gone.
        if (--xr_ < 0) xr_ = P - 1;
        xb_[xr_]     = x;
        xb_[xr_ + P] = x;
        const float* xw = &xb_[xr_];

        for (int p = 0; p < L; ++p) {
            float acc = 0.f;
            for (int j = 0; j < P; ++j)
                acc += up_h_[L * j + p] * xw[j];
            // Newest oversampled sample ends up at ub_[ur_] == u[0].
            if (--ur_ < 0) ur_ = kTaps - 1;
            ub_[ur_]         = shape(acc);
            ub_[ur_ + kTaps] = ub_[ur_];
        }

        // Decimate. dn_h_ is the linear-phase prototype built in Init(),
        // so dn_h_[m] == dn_h_[kTaps-1-m]: fold the symmetric pairs to
        // halve the multiplies. The window is contiguous, so the hot loop
        // has no circular branch and the compiler can pipeline the FMAs.
        const float* u    = &ub_[ur_];
        const int    half = kTaps / 2;
        float        y    = 0.f;
        for (int m = 0; m < half; ++m)
            y += dn_h_[m] * (u[m] + u[kTaps - 1 - m]);
        if (kTaps & 1)  // unpaired centre tap (odd kTaps only)
            y += dn_h_[half] * u[half];
        return y;
    }

  private:
    float up_h_[kTaps];   // interpolation prototype (scaled by L)
    float dn_h_[kTaps];   // decimation prototype (unity DC gain)
    float xb_[2 * P];     // base-rate input history (linear 2x buffer)
    float ub_[2 * kTaps]; // L*Fs-rate shaped history (linear 2x buffer)
    int   xr_;            // newest-sample index into xb_ (counts down)
    int   ur_;            // newest-sample index into ub_ (counts down)
};

} // namespace ott
