# GBA-OTT

An OTT-style 4-band upward + downward compressor, with a tuned harmonic notch bank
and noise gate, compatible with the Electrosmith **patch.Init()** Eurorack
module (Daisy Patch SM).

The firmware targets specifically the use with a **Game Boy Advance running Nanoloop 2**. 
The notch frequencies are chosen specifically to match the
GBA's hum. Other than that, it perfectly works as a general
multiband compressor for any source, but you might want to rework the settings.

## Features

- Per-band OTT compression: peak detection, fixed 3:1 ratio applied both
  upward and downward, per-band makeup.
- Harmonic notch gate: a bank of biquad notches, each either always-on or
  sidechain-masked, pre-tuned to the Game Boy's tonal noise.
- Broadband noise gate with hysteresis for the residual hiss.
- 4-band post EQ (low shelf / two bells / high shelf, +/-12 dB).
- CV-input sidechain ducking; sidechain CV output from the Sub band.
- Output soft clipper (rational saturator), 2x oversampled to suppress
  clipper aliasing on harmonically dense material.
- Paged control surface: 4 knobs reach all 20 parameters, with
  soft-takeover and per-page memory.
- Save / recall of all settings to onboard QSPI flash, no SD card.
- Compile-time CPU profiler over USB serial.

## Requirements

- **GNU Arm Embedded Toolchain** (`arm-none-eabi-gcc`) on `PATH`. The
  Electrosmith "Daisy Toolchain" installer bundles this on Windows.
- **GNU make** and **dfu-util** on `PATH`.
- **libDaisy** and **DaisySP** checked out and built once. The Makefile
  points at them via `LIBDAISY_DIR` / `DAISYSP_DIR` (default
  `../../libDaisy` and `../../DaisySP`, relative to the repo root); adjust
  those if your layout differs.

## Build

Build the two dependencies once, then build this project from the repo
root:

```sh
make -C ../../libDaisy
make -C ../../DaisySP
make -j
```


## Flash

Put the module in DFU mode (hold **BOOT**, tap **RESET**, release
**BOOT**), then:

```sh
make program-dfu
```

or flash using [flash.daisy.audio](https://flash.daisy.audio/).

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

- **CV in jacks**: three are summed into the band thresholds (bipolar,
  centered = no offset). The configured ducking jack (default CV jack 5)
  instead drives sidechain ducking, scaled by `Duck Depth`.
- **CV out 1**: sidechain CV from the Sub-band envelope (0 V when
  the `Sidechain Thr.` knob is at zero, otherwise 0-5 V over a 12 dB range).
- **CV out 2** (pin C1): drives the carrier-board LED via the DAC.
- **Gate ins/outs**: unused.

## Saving and recalling

Holding the button for 1.5 s or more saves all parameters from all pages
 to onboard QSPI Flash. 
 The snapshot is captured in the audio interrupt, but the flash write
happens in the main loop. The LED reports writing on memory with a rapid flash.

At startup the patch loads a saved snapshot if flash holds onem
, otherwise it falls back to the compiled defaults. 

## Tuning

Two files are meant for the end user to edit and play with, then rebuild:

- **`src/config/settings.h`** -- main constants: crossover
  frequencies, gate timings, post EQ frequencies and gain range, and the
  harmonic-gate **notch list**. The notch bank is a list of entries
  so one can easily add, retune or remove them.
- **`src/config/defaults.h`** -- the default parameter values at power-on
  (normalised 0..1 as the knob positions). 
  These hold until you move the related knob in the related page,
  and they are valid only while no savestates are present in the QSPI Flash.


## CPU profiler

`profile.h` is a cycle-counter profiler. The Makefile has it 
**enabled** by default. Comment that line out to disable it.

When on, the main loop prints over USB serial:

```
[profile] CPU avg=18.42% peak=27.31% blocks=10000 period=480000 cyc
```

`period` is the per-block cycle budget. The instrumented hot path adds
about 10 cycles;


## Credits

OTT is a free multiband upward/downward compressor plugin by
[Xfer Records](https://xferrecords.com/). GBA-OTT is an independent,
unaffiliated implementation inspired by it for Eurorack hardware; "OTT" is
a product of Xfer Records and this project is not associated with or
endorsed by them.

Built on [libDaisy](https://github.com/electro-smith/libDaisy) and
[DaisySP](https://github.com/electro-smith/DaisySP) by Electrosmith.
