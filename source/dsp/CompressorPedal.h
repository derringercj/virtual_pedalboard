#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>

/**
    A classic feed-forward peak compressor, written out longhand so every stage
    is visible rather than hidden inside juce::dsp::Compressor.

    Each sample takes this path:

        |x|  ->  dB  ->  gain computer          ->  wanted reduction, in dB
                         (threshold/ratio/knee)
                                   |
                                   v
                      attack + release smoothing
                                   |
                                   v
                 y = x * 10^(-reduction/20) * makeup

    Mono on purpose. A bass rig is a mono chain, and one instance per pedal slot
    is what makes the rest of the pedalboard easy to add later.
*/
class CompressorPedal
{
public:
    //==============================================================================
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        makeupGain.reset (sampleRate, 0.02);
        updateAttackCoeff();
        updateReleaseCoeff();
        reset();
    }

    void reset() noexcept
    {
        currentReductionDb = 0.0f;
        makeupGain.setCurrentAndTargetValue (makeupGain.getTargetValue());
    }

    //==============================================================================
    /** Level above which the compressor starts pulling the signal down. */
    void setThresholdDb (float newThresholdDb) noexcept { thresholdDb = newThresholdDb; }

    /** 4.0f means "4 dB above the threshold comes out as 1 dB above it". */
    void setRatio (float newRatio) noexcept { slope = 1.0f - 1.0f / juce::jmax (1.0f, newRatio); }

    /** Width in dB of the rounded corner around the threshold. 0 = hard knee. */
    void setKneeDb (float newKneeDb) noexcept { kneeDb = juce::jmax (0.0f, newKneeDb); }

    /** Gain applied after compression, to win back the level it took away. */
    void setMakeupDb (float newMakeupDb) noexcept
    {
        makeupGain.setTargetValue (juce::Decibels::decibelsToGain (newMakeupDb));
    }

    void setAttackMs (float newAttackMs) noexcept
    {
        if (! isSame (newAttackMs, attackMs)) { attackMs = newAttackMs; updateAttackCoeff(); }
    }

    void setReleaseMs (float newReleaseMs) noexcept
    {
        if (! isSame (newReleaseMs, releaseMs)) { releaseMs = newReleaseMs; updateReleaseCoeff(); }
    }

    //==============================================================================
    float processSample (float x) noexcept
    {
        // 1. Detector: how loud is this sample, in dB?
        const auto levelDb = juce::Decibels::gainToDecibels (std::abs (x));

        // 2. Gain computer: how much reduction does the static curve ask for?
        const auto wantedReductionDb = computeReductionDb (levelDb);

        // 3. Ballistics: clamp down at the attack rate, let go at the release
        //    rate. One-pole smoothing in the dB domain - this is what gives a
        //    compressor its "feel", far more than the ratio does.
        const auto coeff = wantedReductionDb > currentReductionDb ? attackCoeff : releaseCoeff;
        currentReductionDb = wantedReductionDb + coeff * (currentReductionDb - wantedReductionDb);

        // 4. Apply. Makeup rides its own smoother, or turning the knob zippers.
        return x * juce::Decibels::decibelsToGain (-currentReductionDb) * makeupGain.getNextValue();
    }

    /** dB of reduction happening right now - drives the meter. Always >= 0. */
    float getCurrentReductionDb() const noexcept { return currentReductionDb; }

private:
    //==============================================================================
    /** The static curve, expressed as attenuation in dB. Soft-knee form from
        Reiss & McPherson's digital dynamic range compressor design. */
    float computeReductionDb (float levelDb) const noexcept
    {
        const auto over = levelDb - thresholdDb;

        if (kneeDb > 0.0f && std::abs (over) <= kneeDb * 0.5f)
        {
            const auto t = over + kneeDb * 0.5f;
            return slope * t * t / (2.0f * kneeDb);
        }

        return over > 0.0f ? slope * over : 0.0f;
    }

    /** Time constant -> one-pole coefficient. exp(-1 / (seconds * fs)). */
    float timeToCoeff (float milliseconds) const noexcept
    {
        if (milliseconds <= 0.0f || sampleRate <= 0.0)
            return 0.0f;

        return std::exp (-1.0f / (float) (0.001 * (double) milliseconds * sampleRate));
    }

    void updateAttackCoeff() noexcept  { attackCoeff  = timeToCoeff (attackMs); }
    void updateReleaseCoeff() noexcept { releaseCoeff = timeToCoeff (releaseMs); }

    static bool isSame (float a, float b) noexcept { return std::abs (a - b) < 1.0e-6f; }

    double sampleRate = 44100.0;

    float thresholdDb = -18.0f;
    float slope       = 1.0f - 1.0f / 4.0f;   // ratio 4:1
    float kneeDb      = 6.0f;
    float attackMs    = 10.0f;
    float releaseMs   = 150.0f;

    float attackCoeff       = 0.0f;
    float releaseCoeff      = 0.0f;
    float currentReductionDb = 0.0f;

    juce::SmoothedValue<float> makeupGain { 1.0f };
};
