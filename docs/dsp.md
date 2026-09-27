# Compressor DSP

`source/dsp/CompressorPedal.h`. Header only.

`CompressorPedal` is a feed-forward peak compressor, written out stage by stage
so each part can be seen and tested. It processes one mono sample at a time and
knows nothing about parameters objects, editors or threads. The pedal wrapper,
`CompressorProcessor` ([pedals.md](pedals.md#compressorprocessor)), handles those.

## Signal path

```
x ──► |x| ──► dB ──► gain computer ──► ballistics ──► y = x · gain · makeup
      detector      (threshold,        (attack /
                     ratio, knee)       release)
```

Each sample goes through four stages.

### 1. Detector

```
L = 20 · log10(|x|)        (floored at -100 dB)
```

The level of this one sample, in dB. It's a **peak** detector: it reacts to
each sample's size directly, with no averaging.

### 2. Gain computer (the static curve)

How much reduction the settings ask for at level `L`, as a positive number of dB.

```
slope = 1 − 1/ratio
over  = L − threshold

hard knee (knee = 0), or outside the knee:
    R = slope · over     if over > 0
    R = 0                otherwise

inside the knee (|over| ≤ knee/2):
    R = slope · (over + knee/2)² / (2 · knee)
```

- **Ratio.** At 4:1, `slope` is 0.75. A signal 12 dB over the threshold is
  turned down 9 dB, so it comes out 3 dB over.
- **Knee.** A soft knee rounds the corner at the threshold into a curve
  `knee` dB wide, centred on the threshold. The quadratic meets the straight
  line exactly at both edges, so there's no jump. This is the soft-knee formula
  from Reiss & McPherson's work on digital compressor design.
- The curve never boosts. `R` is always 0 or more.

### 3. Ballistics (attack and release)

The wanted reduction can jump from sample to sample. The ballistics smooth it
so the gain moves at a controlled speed:

```
coeff = attackCoeff    if R_wanted > R_current   (clamping down)
        releaseCoeff   otherwise                 (letting go)

R_current = R_wanted + coeff · (R_current − R_wanted)
```

Each coefficient is a one-pole filter constant:

```
coeff = exp(−1 / (time_in_seconds · sampleRate))      (0 if time ≤ 0: instant)
```

With that coefficient, **one time constant covers 63.2% of the move.** A 10 ms
attack, faced with a sudden 9 dB of wanted reduction, reaches about 5.7 dB after
10 ms. The tests check exactly this.

Smoothing happens on the **reduction in dB**, not on the audio level. That's
what gives the compressor its feel, far more than the ratio does. Fast attack
flattens the pick attack; slow attack lets it through.

### 4. Apply

```
y = x · 10^(−R_current / 20) · makeup
```

Makeup gain is a `juce::SmoothedValue<float>` that ramps over 20 ms whenever the
knob moves, so turning it doesn't make zipper noise.

## Release ripple on low notes

A peak detector sees `|x|` fall to zero twice per cycle of a waveform. Between
peaks, the wanted reduction drops and the release starts letting go, then the
next peak pulls it back down. That makes the gain wobble at twice the note's
frequency.

On bass this matters: low E is about 41 Hz, so its cycles are long, and a short
release has time to recover within one. That wobble is heard as distortion. A
longer release smooths it out. `tests/CompressorTests.cpp` measures the ripple
on a 100 Hz sine at several release times and checks that it shrinks as the
release gets longer.

## API

| Method | Notes |
|--------|-------|
| `prepare (spec)` | Stores the sample rate, sets up the 20 ms makeup ramp, recalculates both coefficients and resets. Call before processing and whenever the sample rate changes. |
| `reset()` | Sets the current reduction to 0 and snaps makeup to its target. |
| `setThresholdDb (dB)` | Takes effect on the next sample. |
| `setRatio (ratio)` | Values below 1 are clamped to 1 (no compression) rather than turned into an expander. |
| `setKneeDb (dB)` | Negative values are clamped to 0 (hard knee). |
| `setMakeupDb (dB)` | Starts a 20 ms ramp to the new gain. |
| `setAttackMs (ms)`, `setReleaseMs (ms)` | Recalculate the coefficient only if the value actually changed, since `exp` isn't free and these are called every block. |
| `processSample (x)` | Runs one sample through all four stages and returns it. |
| `getCurrentReductionDb()` | The current smoothed reduction, always 0 or more. |

Defaults before any setter is called: threshold -18 dB, 4:1, 6 dB knee, 10 ms
attack, 150 ms release, 0 dB makeup.

## Compared with juce::dsp::Compressor

JUCE ships a compressor in `juce_dsp`. This project deliberately doesn't use it.

| | `CompressorPedal` | `juce::dsp::Compressor` |
|---|---|---|
| Soft knee | Yes | No, hard knee only |
| Makeup gain | Yes, smoothed | No |
| Gain reduction readout | `getCurrentReductionDb()` | Not exposed |
| What's smoothed | The gain reduction, in dB | The input level, in linear amplitude |
| Meaning of "10 ms attack" | A 10 ms time constant | About 1.6 ms, because JUCE's coefficient is `exp(−2π·1000 / (fs·ms))` |

JUCE's version is a simple building block, not something more battle-tested.
Switching would lose features for no gain in reliability. The tests are what
make this one trustworthy.
