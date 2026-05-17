#pragma once

// Lightweight CPU-load profiler for the audio callback on Cortex-M7.
// All overhead disappears at compile time when OTT_PROFILE_ENABLED is not set.
//
// Method:
//   Reads the Cortex-M7 DWT cycle counter (CYCCNT) at Begin() and End() and
//   accumulates min/max/sum/count integers. No floating-point in the hot
//   path. The periodic Poll() call (driven from the main loop) snapshots
//   the counters, computes percentages, and emits a single text line via
//   the user-supplied PrintLine function.
//
// Cost when enabled (audio callback):
//   Begin() : ~3 cycles  (1 load from DWT->CYCCNT + 1 store)
//   End()   : ~6 cycles  (1 load + sub + compare + 64-bit add + counter inc)
//
// Usage:
//   1. Build with -DOTT_PROFILE_ENABLED (or define before including this
//      header).
//   2. After patch.Init() but before StartAudio(), call:
//        OTT_PROFILE_INIT(System::GetSysClkFreq(),
//                         patch.AudioBlockSize(),
//                         (uint32_t)patch.AudioSampleRate());
//        patch.StartLog(false);   // bring up USB serial; false = non-blocking
//   3. Bracket the audio callback body:
//        OTT_PROFILE_BEGIN();
//        ... work ...
//        OTT_PROFILE_END();
//   4. Drive reporting from the main loop:
//        static void PrintLine(const char* s) { patch.PrintLine(s); }
//        static uint32_t NowMs()              { return daisy::System::GetNow(); }
//        while (true) { OTT_PROFILE_POLL(PrintLine, NowMs); }
//
// Output line example:
//   [profile] CPU avg=18.42% peak=27.31% blocks=10000 period=480000 cyc

#ifdef OTT_PROFILE_ENABLED

#include <stdint.h>

namespace ott { namespace profile {

// Shared state (defined in profile.cpp). Exposed here so that Begin()/End()
// can stay inline and not pay a call-site cost.
extern uint32_t          s_period_cycles;
extern volatile uint32_t s_start_cyc;
extern volatile uint32_t s_peak_cyc;
extern volatile uint32_t s_count;
extern volatile uint64_t s_sum_cyc;

// Direct DWT CYCCNT read. The register address is fixed on all Cortex-M7
// parts so we don't need to drag in CMSIS headers here.
inline uint32_t ReadCycles()
{
    return *(volatile uint32_t*)0xE0001004UL;
}

inline void Begin()
{
    s_start_cyc = ReadCycles();
}

inline void End()
{
    const uint32_t elapsed = ReadCycles() - s_start_cyc;
    if (elapsed > s_peak_cyc) s_peak_cyc = elapsed;
    s_sum_cyc += elapsed;
    s_count++;
}

typedef void     (*PrintLineFn)(const char*);
typedef uint32_t (*NowMsFn)();

// Enables DWT->CYCCNT and stores the block period (cycles at 100% CPU).
void Init(uint32_t cpu_freq_hz,
          uint32_t block_size,
          uint32_t sample_rate_hz,
          uint32_t report_period_ms = 10000);

// Call from the main loop. Emits one line every report_period_ms; cheap
// otherwise (just one System::GetNow() compare).
void Poll(PrintLineFn print_line, NowMsFn now_ms);

}} // namespace ott::profile

#define OTT_PROFILE_INIT(cpu, blk, sr) ott::profile::Init((cpu), (blk), (sr))
#define OTT_PROFILE_BEGIN()            ott::profile::Begin()
#define OTT_PROFILE_END()              ott::profile::End()
#define OTT_PROFILE_POLL(prt, now)     ott::profile::Poll((prt), (now))

#else // OTT_PROFILE_ENABLED

#define OTT_PROFILE_INIT(...) ((void)0)
#define OTT_PROFILE_BEGIN()   ((void)0)
#define OTT_PROFILE_END()     ((void)0)
#define OTT_PROFILE_POLL(...) ((void)0)

#endif // OTT_PROFILE_ENABLED
