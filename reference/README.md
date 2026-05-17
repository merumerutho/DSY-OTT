# ott_patch

OTT-style 4-band upward+downward compressor for the Electrosmith **patch.Init()**
Eurorack module (Daisy Patch SM).

## Status

MVP **scaffold** — builds and runs as stereo passthrough. The DSP layer
(LR4 crossover + per-band up/down compression + makeup/depth mix) is stubbed
and lands in the next step.

## Control map

Each toggle position has **2 sub-pages**. Tap the button (release before
1.5 s) to advance to the next sub-page. The LED brightness shows which
sub-page is active: **bright = page 1, dim = page 2**. Each toggle position
remembers its own current page — flipping the toggle does not reset it.

| Toggle | Page (LED)    | Button  | Knob 1            | Knob 2          | Knob 3          | Knob 4          |
|--------|---------------|---------|-------------------|-----------------|-----------------|-----------------|
| BANDS  | 1 (**bright**)| up      | Low Threshold     | Lo-Mid Thresh   | Mid-Hi Thresh   | High Threshold  |
| BANDS  | 1 (**bright**)| held    | Low Makeup        | Lo-Mid Makeup   | Mid-Hi Makeup   | High Makeup     |
| BANDS  | 2 (**dim**)   | up      | Post EQ Low       | Post EQ Mid     | Post EQ High    | *(reserved)*    |
| GLOBAL | 1 (**bright**)| up      | Depth (dry/wet)   | Input Gain      | Output Gain     | Time Multiplier |
| GLOBAL | 2 (**dim**)   | up      | Gate Threshold    | Sidechain Thr.  | Duck Depth      | *(reserved)*    |

**Button gestures:**
- Press + release < 1.5 s → advance current toggle's page
- Hold (without releasing) → shift modifier (only meaningful on BANDS page 1)
- Hold ≥ 1.5 s → toggle bypass

- **CV jacks** — three are summed into band thresholds (bipolar, centred = no offset). The configured ducking jack (default **CV jack 5**, set in `settings.h`) drives sidechain ducking instead: positive CV attenuates the processed output, with depth controlled by `Duck Depth` on GLOBAL page 2. With nothing patched, CV reads ~0 V → no ducking.
- **CV out 1** = sidechain CV from the Low band envelope. 0 V when `Sidechain thr.` knob is below 2%; otherwise climbs 0–5 V over a 12 dB range above the threshold (knob maps −60..0 dBFS). Muted while bypassed.
- **Gate ins/outs** unused.
- **LED** PWM-driven. Brightness encodes the current sub-page (bright = page 1, dim = page 2). Blinks at 1 Hz at the same brightness while bypassed.

## Bypass

Hold the **button for ≥ 1.5 s** to toggle bypass. LED switches to a 1 Hz blink while bypassed. Audio passes through (raw input → output, no gain stages, no soft clip) and the sidechain CV is muted. The DSP keeps running internally so envelope state stays warm — re-engaging is transient-free. The dry/processed crossover is smoothed over ~5 ms to keep transitions click-free.

## Pin map (Patch SM indices)

From the Electrosmith patch.Init() v1.0 databrief (MAR/13/2025):

| Panel       | Patch SM pin | libDaisy index   |
|-------------|--------------|------------------|
| Knob 1      | C5           | `CV_1`           |
| Knob 2      | C4           | `CV_2`           |
| Knob 3      | C3           | `CV_3`           |
| Knob 4      | C2           | `CV_4`           |
| CV jack 5   | C6           | `CV_5`           |
| CV jack 6   | C7           | `CV_6`           |
| CV jack 7   | C8           | `CV_7`           |
| CV jack 8   | C9           | `CV_8`           |
| Button      | B7           | GPIO (`Switch`)  |
| Toggle      | B8           | GPIO (`Switch`, TYPE_TOGGLE) |
| CV out 1    | C10          | `CV_OUT_1`       |
| Gate in 1/2 | B10 / B9     | `gate_in_1/2`    |
| Gate out 1/2| B5 / B6      | `gate_out_1/2`   |

## Layout

```
OTT/
├── libDaisy/          # cloned
├── DaisySP/           # cloned
└── ott_patch/
    ├── Makefile
    ├── ott_patch.cpp  # main + audio callback (signal chain wiring)
    ├── settings.h     # all user-tunable defaults in one place
    ├── fast_math.h    # IEEE-754 bit-trick log2 / exp2 / dB conversions
    ├── controls.h     # knobs/button/toggle + page state
    ├── gate.h         # input noise gate (stereo-linked, hysteretic)
    ├── biquad.h       # RBJ biquad (LP/HP/shelves/peak)
    ├── crossover.h    # LR4 4-band splitter (uses biquad)
    ├── ott_band.h     # per-band peak envelope + up/down + makeup
    ├── posteq.h       # 3-band post EQ (low shelf / mid peak / high shelf)
    └── README.md
```

## Tuning

Most things you'd want to tweak — crossover frequencies, gate timings, post
EQ frequencies and gain range — live in **`settings.h`**. Edit values there
and rebuild; the DSP modules pick them up at `Init()`.

## Prerequisites

- **GNU Arm Embedded Toolchain** (`arm-none-eabi-gcc`) on PATH.
  The Electrosmith "Daisy Toolchain" installer bundles this on Windows.
- **GNU make** on PATH.
- **dfu-util** on PATH (for flashing over USB).

## Build

libDaisy and DaisySP must be built once, before this project:

```sh
cd ../libDaisy && make
cd ../DaisySP && make
```

Then build this project:

```sh
cd ott_patch
make
```

Build artefacts land in `ott_patch/build/`.

## Flash

Hold **BOOT** on the Patch SM, tap **RESET**, release **BOOT** — the module
appears as DFU. Then:

```sh
make program-dfu
```

## Roadmap

- [x] LR4 4-band crossover via cascaded RBJ-cookbook biquads (Butterworth Q).
- [x] Per-band peak envelope follower + up/down gain computer + makeup.
- [x] Knob → dB / ms mappings inside DSP layer.
- [x] Input/output gain stages (-24..+24 dB).
- [x] Input noise gate (single-knob threshold, hysteretic, click-free).
- [x] Output soft clip (rational saturator, transparent below 0.9, asymptotes to ±1).
- [x] CV out 1 = sidechain from Low band, threshold-and-scale.
- [x] Bypass (long-press button) with click-free crossfade and LED feedback.
- [x] 3-band post EQ (low shelf / mid peak / high shelf, ±12 dB).
- [x] CV-input sidechain ducking (configurable jack, smoothed gain, true bypass safe).
- [~] DC blocker — not needed; patch.SM analog output is AC-coupled in hardware.
- [ ] Soft-takeover for knobs across page changes.

## DSP defaults

| Parameter           | Range / value                                          |
|---------------------|--------------------------------------------------------|
| Sample rate         | 48 kHz                                                 |
| Block size          | 48 samples (1 ms / 1 kHz callback)                     |
| Crossovers          | 200 Hz, 1 kHz, 5 kHz                                   |
| Detection           | Peak (`max(\|L\|, \|R\|)`), stereo-linked              |
| Threshold mapping   | knob 0..1 → −60..0 dBFS                                |
| Makeup mapping      | knob 0..1 → 0..+24 dB                                  |
| Input/output gain   | knob 0..1 → −24..+24 dB (linear in dB, 0.5 = unity)    |
| Gate threshold      | knob &lt; 0.02 = bypass, else 0..1 → −80..−20 dB        |
| Gate timing (fixed) | 2 ms attack, 100 ms release, 20 ms hold, 6 dB hyst.    |
| Sidechain threshold | knob &lt; 0.02 = mute CV, else 0..1 → −60..0 dBFS         |
| Sidechain range     | 12 dB above threshold → full 5 V (clamped)             |
| Soft clip knee      | 0.9 (transparent below; asymptotic to ±1 above)        |
| Time mult mapping   | knob 0..1 → 0.25× .. 4× (exp scale, 0.5 = nominal)     |
| Nominal attack      | 3 ms                                                   |
| Nominal release     | 80 ms                                                  |
| Ratio (up & down)   | 3:1 (fixed)                                            |
| Upward boost cap    | +24 dB undershoot used (≈ +16 dB max boost at 3:1)     |
