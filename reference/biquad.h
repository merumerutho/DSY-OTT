#pragma once

#include <math.h>

namespace ott {

// Single 2nd-order biquad in Direct Form II Transposed, with RBJ-cookbook
// coefficient computation for lowpass, highpass, low-shelf, high-shelf, and
// peaking-EQ responses.
//
// DF2T is the numerically stable choice for static coefficients on float math;
// per-sample cost is 5 multiplies, 4 adds.
//
// Used for both the LR4 crossover (LP/HP at Butterworth Q) and the post EQ
// (shelves and peaking, with adjustable gain in dB).
struct Biquad {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f;
    float a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;

    enum class Type { Lowpass, Highpass };

    // Lowpass / Highpass at Butterworth Q (1/sqrt(2)). Cascading two of these
    // at the same fc yields LR4.
    void Set(Type type, float fc, float sample_rate) {
        constexpr float kQ = 0.70710678118654752f;
        const float w0    = 2.f * 3.14159265358979323f * fc / sample_rate;
        const float cos_w = cosf(w0);
        const float sin_w = sinf(w0);
        const float alpha = sin_w / (2.f * kQ);

        float B0, B1, B2;
        if (type == Type::Lowpass) {
            B0 = (1.f - cos_w) * 0.5f;
            B1 = 1.f - cos_w;
            B2 = (1.f - cos_w) * 0.5f;
        } else { // Highpass
            B0 = (1.f + cos_w) * 0.5f;
            B1 = -(1.f + cos_w);
            B2 = (1.f + cos_w) * 0.5f;
        }
        const float A0 = 1.f + alpha;
        const float A1 = -2.f * cos_w;
        const float A2 = 1.f - alpha;

        Normalize(B0, B1, B2, A0, A1, A2);
    }

    // Low shelf: bass tilt centred at fc, gain in dB applied below fc.
    // Slope S = 1 (RBJ default, ~12 dB/oct shelving slope).
    void SetLowShelf(float fc, float gain_db, float sample_rate) {
        constexpr float kS = 1.f;
        const float A     = powf(10.f, gain_db * (1.f / 40.f));
        const float w0    = 2.f * 3.14159265358979323f * fc / sample_rate;
        const float cos_w = cosf(w0);
        const float sin_w = sinf(w0);
        const float alpha = sin_w * 0.5f
                          * sqrtf((A + 1.f / A) * (1.f / kS - 1.f) + 2.f);
        const float two_sqrtA_alpha = 2.f * sqrtf(A) * alpha;

        const float B0 = A * ((A + 1.f) - (A - 1.f) * cos_w + two_sqrtA_alpha);
        const float B1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cos_w);
        const float B2 = A * ((A + 1.f) - (A - 1.f) * cos_w - two_sqrtA_alpha);
        const float A0 = (A + 1.f) + (A - 1.f) * cos_w + two_sqrtA_alpha;
        const float A1 = -2.f * ((A - 1.f) + (A + 1.f) * cos_w);
        const float A2 = (A + 1.f) + (A - 1.f) * cos_w - two_sqrtA_alpha;

        Normalize(B0, B1, B2, A0, A1, A2);
    }

    // High shelf: treble tilt centred at fc, gain in dB applied above fc.
    void SetHighShelf(float fc, float gain_db, float sample_rate) {
        constexpr float kS = 1.f;
        const float A     = powf(10.f, gain_db * (1.f / 40.f));
        const float w0    = 2.f * 3.14159265358979323f * fc / sample_rate;
        const float cos_w = cosf(w0);
        const float sin_w = sinf(w0);
        const float alpha = sin_w * 0.5f
                          * sqrtf((A + 1.f / A) * (1.f / kS - 1.f) + 2.f);
        const float two_sqrtA_alpha = 2.f * sqrtf(A) * alpha;

        const float B0 = A * ((A + 1.f) + (A - 1.f) * cos_w + two_sqrtA_alpha);
        const float B1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cos_w);
        const float B2 = A * ((A + 1.f) + (A - 1.f) * cos_w - two_sqrtA_alpha);
        const float A0 = (A + 1.f) - (A - 1.f) * cos_w + two_sqrtA_alpha;
        const float A1 = 2.f * ((A - 1.f) - (A + 1.f) * cos_w);
        const float A2 = (A + 1.f) - (A - 1.f) * cos_w - two_sqrtA_alpha;

        Normalize(B0, B1, B2, A0, A1, A2);
    }

    // Notch (band-stop) at fc with bandwidth Q. Hits zero gain at fc.
    // Higher Q -> narrower notch.
    void SetNotch(float fc, float Q, float sample_rate) {
        const float w0    = 2.f * 3.14159265358979323f * fc / sample_rate;
        const float cos_w = cosf(w0);
        const float sin_w = sinf(w0);
        const float alpha = sin_w / (2.f * Q);

        const float B0 = 1.f;
        const float B1 = -2.f * cos_w;
        const float B2 = 1.f;
        const float A0 = 1.f + alpha;
        const float A1 = -2.f * cos_w;
        const float A2 = 1.f - alpha;

        Normalize(B0, B1, B2, A0, A1, A2);
    }

    // Bandpass at fc with bandwidth Q ("constant 0 dB peak gain" form).
    // Higher Q -> narrower passband. DC and Nyquist see zero gain.
    void SetBandpass(float fc, float Q, float sample_rate) {
        const float w0    = 2.f * 3.14159265358979323f * fc / sample_rate;
        const float cos_w = cosf(w0);
        const float sin_w = sinf(w0);
        const float alpha = sin_w / (2.f * Q);

        const float B0 = alpha;
        const float B1 = 0.f;
        const float B2 = -alpha;
        const float A0 = 1.f + alpha;
        const float A1 = -2.f * cos_w;
        const float A2 = 1.f - alpha;

        Normalize(B0, B1, B2, A0, A1, A2);
    }

    // Peaking (bell) EQ at fc with bandwidth Q and gain in dB.
    void SetPeaking(float fc, float gain_db, float Q, float sample_rate) {
        const float A     = powf(10.f, gain_db * (1.f / 40.f));
        const float w0    = 2.f * 3.14159265358979323f * fc / sample_rate;
        const float cos_w = cosf(w0);
        const float sin_w = sinf(w0);
        const float alpha = sin_w / (2.f * Q);

        const float B0 = 1.f + alpha * A;
        const float B1 = -2.f * cos_w;
        const float B2 = 1.f - alpha * A;
        const float A0 = 1.f + alpha / A;
        const float A1 = -2.f * cos_w;
        const float A2 = 1.f - alpha / A;

        Normalize(B0, B1, B2, A0, A1, A2);
    }

    inline float Process(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void Reset() { z1 = z2 = 0.f; }

  private:
    inline void Normalize(float B0, float B1, float B2,
                          float A0, float A1, float A2) {
        const float inv_a0 = 1.f / A0;
        b0 = B0 * inv_a0;
        b1 = B1 * inv_a0;
        b2 = B2 * inv_a0;
        a1 = A1 * inv_a0;
        a2 = A2 * inv_a0;
    }
};

} // namespace ott
