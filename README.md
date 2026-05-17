# GBA-OTT

An OTT-style 4-band upward + downward compressor, with a tuned noise gate
and harmonic notch bank, for the Electrosmith **patch.Init()** Eurorack
module (Daisy Patch SM).

It was conceived for one specific job: cleaning up and fattening the audio
out of a **Game Boy Advance running Nanoloop 2**. The notch frequencies,
the noise gate, and the power-on knob positions are all chosen for that
rig's hum, clock leakage, and switching whine. It still works as a general
multiband compressor for any lo-fi or noisy source, but the defaults are
Game Boy shaped, and they are meant to be retuned for your own setup.

> Inspired by **OTT**, the free multiband compressor by **Xfer Records**.
> This is an independent, unaffiliated reimplementation; "OTT" is a
> product of Xfer Records and this project is not associated with or
> endorsed by them. See [Credits](#credits).

## Contents

- [Signal chain](#signal-chain)
- [Features](#features)
- [Requirements](#requirements)
- [Build](#build)
- [Flash](#flash)
- [Controls](#controls)
- [Saving and recall](#saving-and-recall)
- [Tuning it yourself](#tuning-it-yourself)
- [CPU profiler](#cpu-profiler)
- [Hardware pin map](#hardware-pin-map)
- [DSP reference](#dsp-reference)
- [Credits](#credits)

## Signal chain

```
   Game Boy Advance (Nanoloop 2)
                |
                v          line in
          +-------------+
          | input gain  |
          +-------------+
                |
                v
        +-----------------+   notch bank tuned to the GBA:
        |  harmonic gate  |   mains hum, clock ladder,
        +-----------------+   SMPS whine, ultrasonic
                |
                v
        +-----------------+   removes residual hiss
        |   noise gate    |   between notes
        +-----------------+
                |
                v
     +-----------------------+
     |  LR4 4-band split     |   Sub | Bass | Mid | High
     +-----------------------+   (80 Hz / 300 Hz / 3 kHz)
                |
                v
     +-----------------------+
     |  per-band OTT         |   peak env, 3:1 up + down,
     |  (x4 bands)           |   per-band makeup
     +-----------------------+
                |
                v
   sum -> depth (dry/wet) -> output gain
                |
                v
   4-band post EQ -> CV duck -> soft clip -> out
```

## Features

- Per-band OTT compression: peak detection, fixed 3:1 ratio applied both
  upward and downward, per-band makeup.
- Harmonic notch gate: a bank of biquad notches, each either always-on or
  sidechain-masked, pre-tuned to the Game Boy's tonal noise.
- Broadband noise gate with hysteresis for the residual hiss.
- 4-band post EQ (low shelf / two bells / high shelf, +/-12 dB).
- CV-input sidechain ducking; sidechain CV output from the Sub band.
- Output soft clipper (rational saturator).
- Paged control surface: 4 knobs reach all 20 parameters, with
  soft-takeover and per-page memory.
- Save / recall of all settings to onboard QSPI flash, no SD card.
- Optional compile-time CPU profiler over USB serial.

## Requirements

- **GNU Arm Embedded Toolchain** (`arm-none-eabi-gcc`) on `PATH`. The
  Electrosmith "Daisy Toolchain" installer bundles this on Windows.
- **GNU make** and **dfu-util** on `PATH`.
- **libDaisy** and **DaisySP** checked out next to this project and built
  once. The Makefile expects them at `../libDaisy` and `../DaisySP`.

## Build

```sh
cd ../libDaisy && make      # once
cd ../DaisySP  && make      # once
cd ../ott_patch && make
```

Artifacts are written to `build/`. Run `make clean` whenever you toggle a
compile-time define (object timestamps do not change just because a `-D`
did).

## Flash

Put the module in DFU mode (hold **BOOT**, tap **RESET**, release
**BOOT**), then:

```sh
make program-dfu
```

## Controls

The toggle selects a group (BANDS or GLOBAL). Each group has sub-pages
(BANDS has 3, GLOBAL has 2). A short button press cycles to the next
sub-page within the current group; each group remembers its own page. The
LED encodes the active page: **solid = page 1**, **fast blink = page 2**,
**slow blink = page 3**.

| Toggle | Page (LED)     | Knob 1          | Knob 2         | Knob 3        | Knob 4          |
|--------|----------------|-----------------|----------------|---------------|-----------------|
| BANDS  | 1 (solid)      | Sub Threshold   | Bass Threshold | Mid Threshold | High Threshold  |
| BANDS  | 2 (fast blink) | Sub Makeup      | Bass Makeup    | Mid Makeup    | High Makeup     |
| BANDS  | 3 (slow blink) | Post EQ Sub     | Post EQ Bass   | Post EQ Mid   | Post EQ High    |
| GLOBAL | 1 (solid)      | Depth (dry/wet) | Input Gain     | Output Gain   | Time Multiplier |
| GLOBAL | 2 (fast blink) | Gate Threshold  | Sidechain Thr. | Duck Depth    | Notch Depth     |

**Button gestures**

- Press and release in under 1.5 s: advance the current group's page.
- Hold for 1.5 s or more: save all pages to flash (the LED flashes
  rapidly to confirm).

**Soft-takeover.** A knob is ignored until it is moved into a small window
around the stored value, so changing pages, recalling saved state, or
powering on never causes value jumps. The boot values are heard until a
knob is touched.

**CV and gates**

- **CV jacks**: three are summed into the band thresholds (bipolar,
  centred = no offset). The configured ducking jack (default CV jack 5)
  instead drives sidechain ducking, scaled by `Duck Depth`.
- **CV out 1**: sidechain CV from the Sub-band envelope (0 V below 2% on
  the `Sidechain Thr.` knob, then 0-5 V over a 12 dB range).
- **CV out 2** (pin C1): drives the carrier-board LED via the DAC.
- **Gate ins/outs**: unused.

## Saving and recall

Holding the button for 1.5 s or more saves every page's values at once
(the whole knob grid, not individual knobs or one page) to onboard QSPI
flash. The snapshot is captured in the audio interrupt; the flash write
happens in the main loop. The LED confirms with a rapid flash lasting
under a second. A save only rewrites flash when something changed.

At startup the patch loads a saved snapshot if flash holds one (validated
by a magic word and version tag); otherwise it falls back to the compiled
defaults. Recalled values respect soft-takeover.

## Tuning it yourself

The defaults are a Game Boy starting point, not gospel. Two files are
meant for you to edit and play with, then rebuild:

- **`src/config/settings.h`** -- static DSP constants: crossover
  frequencies, gate timings, post EQ frequencies and gain range, and the
  harmonic-gate **notch list**. The notch bank is templated on the list
  size, so adding or removing entries in `settings::notch::kNotches[]`
  resizes everything at compile time. Retune these for whatever device or
  noise profile you are feeding it.
- **`src/config/defaults.h`** -- the power-on position of every knob
  (normalised 0..1). These hold (via soft-takeover) until you move a knob,
  and they are also the "factory" values restored when flash has no saved
  snapshot.

The DSP modules read both at `Init()`, so a rebuild is all it takes.

`fast_math.h` carries optional bit-trick `log2`/`exp2` kernels. By default
the build uses the libm reference path; define `USE_FAST_MATH` (uncomment
the line in the Makefile, or pass `OTT_EXTRA_DEFS=-DUSE_FAST_MATH`) for the
faster approximations in production builds.

## CPU profiler

`profile.h` is a Cortex-M7 cycle-counter profiler around the audio
callback, gated by `OTT_PROFILE_ENABLED` and **off by default**. Enable it
for a sticky build by uncommenting `C_DEFS += -DOTT_PROFILE_ENABLED` in
the Makefile, or per build:

```sh
make clean && make OTT_EXTRA_DEFS=-DOTT_PROFILE_ENABLED program-dfu
```

It must be `C_DEFS`, not `CPPFLAGS` (libDaisy's core Makefile reassigns
`CPPFLAGS` after the include). When on, the main loop prints one line
every 10 s over USB serial (the Daisy enumerates as a virtual COM port;
baud is irrelevant):

```
[profile] CPU avg=18.42% peak=27.31% blocks=10000 period=480000 cyc
```

`period` is the per-block cycle budget. The instrumented hot path adds
about 10 cycles; everything compiles to no-ops when disabled.

## Hardware pin map

From the Electrosmith patch.Init() v1.0 databrief (2025-03-13):

| Panel          | Patch SM pin | libDaisy index               |
|----------------|--------------|------------------------------|
| Knob 1         | C5           | `CV_1`                       |
| Knob 2         | C4           | `CV_2`                       |
| Knob 3         | C3           | `CV_3`                       |
| Knob 4         | C2           | `CV_4`                       |
| CV jack 5      | C6           | `CV_5`                       |
| CV jack 6      | C7           | `CV_6`                       |
| CV jack 7      | C8           | `CV_7`                       |
| CV jack 8      | C9           | `CV_8`                       |
| Button         | B7           | GPIO (`Switch`)              |
| Toggle         | B8           | GPIO (`Switch`, TYPE_TOGGLE) |
| CV out 1       | C10          | `CV_OUT_1`                   |
| CV out 2 (LED) | C1           | `CV_OUT_2`                   |
| Gate in 1/2    | B10 / B9     | `gate_in_1/2`                |
| Gate out 1/2   | B5 / B6      | `gate_out_1/2`               |

## DSP reference

| Parameter           | Range / value                                                              |
|---------------------|----------------------------------------------------------------------------|
| Sample rate         | 48 kHz                                                                     |
| Block size          | 48 samples (1 ms / 1 kHz callback)                                         |
| Bands               | Sub / Bass / Mid / High                                                    |
| Crossovers          | 80 Hz, 300 Hz, 3 kHz                                                       |
| Post EQ             | Sub low shelf @ 80 Hz, Bass bell @ 150 Hz, Mid bell @ 950 Hz, High shelf @ 3 kHz; +/-12 dB |
| Detection           | Peak (`max(\|L\|, \|R\|)`), stereo-linked                                  |
| Threshold mapping   | knob 0..1 to -60..0 dBFS                                                   |
| Makeup mapping      | knob 0..1 to 0..+24 dB                                                     |
| Input/output gain   | knob 0..1 to -24..+24 dB (linear in dB, 0.5 = unity)                       |
| Gate threshold      | knob < 0.02 = bypass, else 0..1 to -80..-20 dB                             |
| Gate timing (fixed) | 2 ms attack, 100 ms release, 20 ms hold, 6 dB hysteresis                   |
| Sidechain threshold | knob < 0.02 = mute CV, else 0..1 to -60..0 dBFS                            |
| Sidechain range     | 12 dB above threshold to full 5 V (clamped)                               |
| Soft clip knee      | 0.9 (transparent below; asymptotic to +/-1 above)                          |
| Time mult mapping   | knob 0..1 to 0.25x .. 4x (exponential, 0.5 = nominal)                      |
| Nominal attack      | 3 ms                                                                       |
| Nominal release     | 80 ms                                                                      |
| Ratio (up & down)   | 3:1 (fixed)                                                                |
| Upward boost cap    | +24 dB undershoot used (about +16 dB max boost at 3:1)                     |
| Harmonic gate       | 11 notches by default (60 Hz family, clock ladder, SMPS, ultrasonic)       |
| Notch depth mapping | knob 0..1 to 0..full notch strength                                        |

## Credits

OTT is a free multiband upward/downward compressor plugin by
[Xfer Records](https://xferrecords.com/). GBA-OTT is an independent,
unaffiliated implementation inspired by it for Eurorack hardware; "OTT" is
a product of Xfer Records and this project is not associated with or
endorsed by them.

Built on [libDaisy](https://github.com/electro-smith/libDaisy) and
[DaisySP](https://github.com/electro-smith/DaisySP) by Electrosmith.
