#pragma once

#include <stdint.h>

namespace ott {

// Single-precision fast log2 / exp2 using IEEE-754 bit manipulation.
//
// Same style as the LibDspStdC InvFast / SqrtFast references:
//   - reinterpret float bits via a union
//   - manipulate the exponent in integer math
//   - apply a Horner-form polynomial of the mantissa (in [1, 2)) for the
//     fine correction.
//
// Cost on Cortex-M7 (rough):
//   FastLog2  : ~17 cycles (4-FMA polynomial; vs ~75 for libm log10f)
//   FastExp2  : ~19 cycles (no transcendental; vs ~150 for libm powf)
//
// Accuracy: ~1e-4 absolute error in log2; ~1e-4 relative error in 2^x.
// Translated to dB via the fold-in scaling factors below, both stay
// around 0.001 dB worst case -- inaudible for compressor gain math.
//
// Note: the M7 has no float SIMD (no NEON, no Helium). The bit tricks pay
// off via raw cycle count, not parallelism.

typedef union {
    float    f;
    uint32_t u;
} U_FloatBits_t;

// ---------------------------------------------------------------------------
// FastLog2
// ---------------------------------------------------------------------------
// Extracts the IEEE-754 biased exponent for the integer part of log2(x),
// then approximates log2(mantissa) on [1, 2] with a 4th-order near-minimax
// polynomial. The polynomial comes from a 10-node Chebyshev expansion
// truncated at degree 4 -- the residual error oscillates across 6 extrema
// with magnitudes 6.7e-5 to 1.0e-4, close to true Remez-equiripple. This
// replaces the older arctanh-substitution form, which needed a float
// divide (~14 cycles on M7) for similar accuracy.
inline float FastLog2(float x)
{
    U_FloatBits_t fastlog_x = { .f = x };

    // Extract the IEEE-754 8-bit biased exponent and unbias to get the
    // integer part of log2(x).
    const int32_t e = ((int32_t)(fastlog_x.u >> 23) & 0xFF) - 127;

    // Isolate the input mantissa, leaving a value in [1, 2)
    // (force the exponent bits to 127, which is 2^0).
    fastlog_x.u = (fastlog_x.u & 0x7FFFFFU) | 0x3F800000U;
    const float m = fastlog_x.f;

    // 4th-order near-minimax polynomial approximating log2(m) on [1, 2].
    // Max abs error ~1.0e-4 (slightly better than the prior arctanh form).
    constexpr float kC4 = -0.081920f;
    constexpr float kC3 =  0.646656f;
    constexpr float kC2 = -2.123264f;
    constexpr float kC1 =  4.071792f;
    constexpr float kC0 = -2.513162f;
    const float poly = kC0 + m * (kC1 + m * (kC2 + m * (kC3 + m * kC4)));

    return (float)e + poly;
}

// ---------------------------------------------------------------------------
// FastExp2
// ---------------------------------------------------------------------------
// Splits x = xi + m, xi in Z, m in [1, 2). Builds 2^xi by inserting
// (xi + 127) into the exponent bits of a fresh float, and multiplies by a
// 3rd-order minimax polynomial of 2^m on [1, 2]. The minimax distributes
// error uniformly across the interval rather than concentrating it near
// the upper boundary the way a Taylor expansion does, so we get ~7x lower
// worst-case relative error than the previous 4th-order Taylor while
// costing one fewer FMA.
inline float FastExp2(float x)
{
    // Out-of-range clamp (no Inf/NaN escape). Lower bound is one bit
    // tighter than the IEEE normal minimum (-126) because this split
    // sets xi = floor(x) - 1, so x = -126 would push the bit-trick
    // exponent into the subnormal range. The lost [2^-126, 2^-125)
    // window corresponds to dB inputs below -750, far outside any
    // realistic compressor gain.
    if (x < -125.f) return 0.f;
    if (x >  127.f) return 3.4028235e38f; // float max

    // Branch-free floor for x in [-1022, +127]. The bias trick: shift the
    // input into a positive integer range, truncate, then shift back.
    // We add (kBias - 1) rather than kBias so the split lands m = x - xi
    // in [1, 2) -- the domain of the minimax polynomial below.
    // For x = -0.5: (int32_t)(1022.5) = 1022, then 1022 - 1024 = -2,
    // giving m = -0.5 - (-2) = 1.5.
    constexpr int32_t kBias = 1024;
    const int32_t xi = (int32_t)(x + (float)(kBias - 1)) - kBias;
    const float   m  = x - (float)xi;       // m in [1, 2)

    // 3rd-order minimax polynomial approximating 2^m on [1, 2], sourced
    // from a MATLAB minimax fit. Max relative error ~1e-4 across the
    // entire interval (vs ~7.5e-4 at the upper edge for the prior
    // 4th-order Taylor on [0, 1)).
    constexpr float kC3 =  0.1584f;
    constexpr float kC2 = -0.0265f;
    constexpr float kC1 =  0.9708f;
    constexpr float kC0 =  0.8971f;
    const float poly = kC0 + m * (kC1 + m * (kC2 + m * kC3));

    // Build 2^xi by inserting the biased exponent into a fresh float.
    U_FloatBits_t fastexp_out;
    fastexp_out.u = ((uint32_t)(xi + 127)) << 23;

    return fastexp_out.f * poly;
}

// ---------------------------------------------------------------------------
// dB <-> linear convenience wrappers
// ---------------------------------------------------------------------------
// 20 * log10(x) folded into FastLog2:
//   20 * log10(x) = 20 * log10(2) * log2(x) = 6.0206 * log2(x)
inline float FastDbFromLinear(float linear)
{
    constexpr float k = 6.020599913279624f;  // 20 * log10(2)
    return k * FastLog2(linear);
}

// 10^(db / 20) folded into FastExp2:
//   10^(db/20) = 2^(db * log2(10) / 20) = 2^(db * 0.166096)
inline float FastLinearFromDb(float db)
{
    constexpr float k = 0.166096404744368f;  // log2(10) / 20
    return FastExp2(db * k);
}

} // namespace ott
