#ifdef OTT_PROFILE_ENABLED

#include "profile.h"
#include <stdio.h>

namespace ott { namespace profile {

uint32_t          s_period_cycles = 1;   // 1 to avoid div-by-zero before Init
volatile uint32_t s_start_cyc     = 0;
volatile uint32_t s_peak_cyc      = 0;
volatile uint32_t s_count         = 0;
volatile uint64_t s_sum_cyc       = 0;

static uint32_t s_report_ms      = 10000;
static uint32_t s_last_report_ms = 0;

// DWT / DEMCR register addresses (Cortex-M7 ARMv7-M).
static constexpr uint32_t kDemcrAddr    = 0xE000EDFCUL;
static constexpr uint32_t kDwtCtrlAddr  = 0xE0001000UL;
static constexpr uint32_t kDwtCycntAddr = 0xE0001004UL;

static inline void DisableIrq() { __asm volatile("cpsid i" ::: "memory"); }
static inline void EnableIrq()  { __asm volatile("cpsie i" ::: "memory"); }

void Init(uint32_t cpu_freq_hz,
          uint32_t block_size,
          uint32_t sample_rate_hz,
          uint32_t report_period_ms)
{
    // Cycles available per audio block at 100% CPU. 64-bit intermediate
    // because cpu_freq * block_size overflows uint32_t for typical configs.
    s_period_cycles = (uint32_t)(((uint64_t)cpu_freq_hz * block_size) / sample_rate_hz);
    if (s_period_cycles == 0) s_period_cycles = 1;

    s_report_ms      = report_period_ms;
    s_last_report_ms = 0;

    // Enable the DWT cycle counter. TRCENA must be set in DEMCR first,
    // then CYCCNTENA in DWT_CTRL.
    *(volatile uint32_t*)kDemcrAddr    |= (1UL << 24);
    *(volatile uint32_t*)kDwtCycntAddr  = 0;
    *(volatile uint32_t*)kDwtCtrlAddr  |= 1UL;

    s_peak_cyc = 0;
    s_sum_cyc  = 0;
    s_count    = 0;
}

void Poll(PrintLineFn print_line, NowMsFn now_ms)
{
    const uint32_t now = now_ms();
    if ((now - s_last_report_ms) < s_report_ms) return;
    s_last_report_ms = now;

    // Atomically snapshot + reset the shared counters. The audio ISR may
    // fire mid-read of the 64-bit sum otherwise.
    DisableIrq();
    const uint64_t local_sum   = s_sum_cyc;
    const uint32_t local_peak  = s_peak_cyc;
    const uint32_t local_count = s_count;
    s_sum_cyc  = 0;
    s_peak_cyc = 0;
    s_count    = 0;
    EnableIrq();

    if (local_count == 0) return;

    // Integer-only formatting: newlib-nano's printf (--specs=nano.specs)
    // parses and consumes %f but renders nothing. Compute load in
    // hundredths of a percent with 64-bit intermediates, then print as
    // "<int>.<2-digit>". Truncation error is < 0.01%.
    const uint64_t period = (uint64_t)s_period_cycles;
    const uint64_t denom  = (uint64_t)local_count * period;
    const uint32_t avg_x100 =
        denom ? (uint32_t)((10000ull * local_sum) / denom) : 0;
    const uint32_t peak_x100 =
        period ? (uint32_t)((10000ull * (uint64_t)local_peak) / period) : 0;

    char buf[128];
    snprintf(buf, sizeof(buf),
             "[profile] CPU avg=%lu.%02lu%% peak=%lu.%02lu%% "
             "blocks=%lu period=%lu cyc",
             (unsigned long)(avg_x100 / 100),  (unsigned long)(avg_x100 % 100),
             (unsigned long)(peak_x100 / 100), (unsigned long)(peak_x100 % 100),
             (unsigned long)local_count,
             (unsigned long)s_period_cycles);
    print_line(buf);
}

}} // namespace ott::profile

#endif // OTT_PROFILE_ENABLED
