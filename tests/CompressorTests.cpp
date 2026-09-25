/*
    Checks on CompressorPedal, against numbers worked out by hand rather than
    against whatever the code happened to produce.

    Build and run:
        cmake --build build --target vpb_tests
        ./build/vpb_tests

    Note the static curve is measured with a *constant* input, not a sine. A peak
    detector sees |x| fall to zero twice per cycle, so a sine makes the gain creep
    back up between peaks and the measured reduction lands slightly under the
    curve. That ripple is real compressor behaviour, and it gets its own test.
*/

#include "dsp/CompressorPedal.h"
#include "TestUtils.h"

#include <cmath>
#include <cstdio>

namespace
{
    CompressorPedal makePedal (double sampleRate, float thresholdDb, float ratio, float kneeDb,
                               float attackMs, float releaseMs, float makeupDb)
    {
        CompressorPedal pedal;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = 512;
        spec.numChannels = 1;

        pedal.setThresholdDb (thresholdDb);
        pedal.setRatio (ratio);
        pedal.setKneeDb (kneeDb);
        pedal.setAttackMs (attackMs);
        pedal.setReleaseMs (releaseMs);
        pedal.setMakeupDb (makeupDb);
        pedal.prepare (spec);

        return pedal;
    }

    constexpr double sampleRate = 48000.0;

    float dbToAmp (double db) { return (float) std::pow (10.0, db / 20.0); }

    /** Hold a constant level until the ballistics settle, then report the
        reduction the static curve settled on. */
    double settledReductionDb (CompressorPedal& pedal, double inputDb)
    {
        const auto amp = dbToAmp (inputDb);

        for (int i = 0; i < (int) (sampleRate * 2.0); ++i)
            pedal.processSample (amp);

        return pedal.getCurrentReductionDb();
    }

    /** Same, but reporting the output level so makeup gain is included. */
    double settledOutputDb (CompressorPedal& pedal, double inputDb)
    {
        const auto amp = dbToAmp (inputDb);
        float out = 0.0f;

        for (int i = 0; i < (int) (sampleRate * 2.0); ++i)
            out = pedal.processSample (amp);

        return 20.0 * std::log10 ((double) std::abs (out));
    }
}

void runCompressorTests()
{
    // --- Static curve: threshold -18 dB, 4:1, hard knee ------------------------
    std::puts ("Static curve, hard knee, threshold -18 dB:");
    {
        // 12 dB over threshold at 4:1 -> 12 * (1 - 1/4) = 9 dB of reduction.
        auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 0.0f, 1.0f, 50.0f, 0.0f);
        test::check ("-6 dBFS in, 4:1", settledReductionDb (pedal, -6.0), 9.0, 0.001);
    }
    {
        auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 0.0f, 1.0f, 50.0f, 0.0f);
        test::check ("-30 dBFS in, below threshold", settledReductionDb (pedal, -30.0), 0.0, 0.001);
    }
    {
        auto pedal = makePedal (sampleRate, -18.0f, 1.0f, 0.0f, 1.0f, 50.0f, 0.0f);
        test::check ("ratio 1:1 is transparent", settledReductionDb (pedal, -6.0), 0.0, 0.001);
    }
    {
        // 12 dB over at 20:1 -> 12 * 0.95 = 11.4 dB.
        auto pedal = makePedal (sampleRate, -18.0f, 20.0f, 0.0f, 1.0f, 50.0f, 0.0f);
        test::check ("-6 dBFS in, 20:1", settledReductionDb (pedal, -6.0), 11.4, 0.001);
    }
    {
        // Ratios below 1:1 are nonsense and must be clamped, not inverted.
        auto pedal = makePedal (sampleRate, -18.0f, 0.25f, 0.0f, 1.0f, 50.0f, 0.0f);
        test::check ("ratio below 1:1 clamps to transparent",
               settledReductionDb (pedal, -6.0), 0.0, 0.001);
    }
    {
        // -6 dBFS in, 9 dB down, 6 dB back up -> -9 dBFS out.
        auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 0.0f, 1.0f, 50.0f, 6.0f);
        test::check ("+6 dB makeup lands on the output", settledOutputDb (pedal, -6.0), -9.0, 0.01);
    }

    // --- Soft knee, 12 dB wide, centred on the threshold -----------------------
    std::puts ("\nSoft knee, 12 dB wide, threshold -18 dB, 4:1:");
    {
        auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 12.0f, 1.0f, 50.0f, 0.0f);
        test::check ("-24 dBFS in, lower knee edge", settledReductionDb (pedal, -24.0), 0.0, 0.001);
    }
    {
        // At the threshold: slope * (W/2)^2 / (2W) = 0.75 * 36 / 24.
        auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 12.0f, 1.0f, 50.0f, 0.0f);
        test::check ("-18 dBFS in, exactly at threshold", settledReductionDb (pedal, -18.0), 1.125, 0.001);
    }
    {
        // At the upper edge the knee must rejoin the hard-knee line: 0.75 * 6.
        auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 12.0f, 1.0f, 50.0f, 0.0f);
        test::check ("-12 dBFS in, upper knee edge is continuous",
               settledReductionDb (pedal, -12.0), 4.5, 0.001);
    }
    {
        auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 12.0f, 1.0f, 50.0f, 0.0f);
        test::check ("-6 dBFS in, above the knee", settledReductionDb (pedal, -6.0), 9.0, 0.001);
    }
    {
        // The curve must never boost, at any input level, knee or no knee.
        auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 12.0f, 0.1f, 5.0f, 0.0f);
        bool everNegative = false;

        for (int db = -90; db <= 0; ++db)
            everNegative = everNegative || settledReductionDb (pedal, (double) db) < 0.0;

        test::checkTrue ("reduction is never negative across -90..0 dBFS", ! everNegative);
    }

    // --- Ballistics ------------------------------------------------------------
    std::puts ("\nBallistics (one time constant should cover 63.2% of the move):");
    {
        auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 0.0f, 10.0f, 200.0f, 0.0f);
        const auto amp = dbToAmp (-6.0);

        for (int i = 0; i < (int) (sampleRate * 0.010); ++i)   // exactly one 10 ms attack
            pedal.processSample (amp);

        test::check ("after one 10 ms attack constant", pedal.getCurrentReductionDb(), 9.0 * 0.632, 0.05);
    }
    {
        auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 0.0f, 1.0f, 100.0f, 0.0f);
        const auto settled = settledReductionDb (pedal, -6.0);

        for (int i = 0; i < (int) (sampleRate * 0.100); ++i)   // exactly one 100 ms release
            pedal.processSample (0.0f);

        test::check ("after one 100 ms release constant",
               pedal.getCurrentReductionDb(), settled * (1.0 - 0.632), 0.05);
    }

    // --- Release ripple on a sine ---------------------------------------------
    std::puts ("\nRelease ripple, 100 Hz sine at -6 dBFS (longer release = less ripple):");
    {
        double previousRipple = 1e9;
        bool shrinking = true;

        for (const float releaseMs : { 20.0f, 50.0f, 200.0f, 1000.0f })
        {
            auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 0.0f, 1.0f, releaseMs, 0.0f);
            const auto amp = dbToAmp (-6.0);

            double lowest = 1e9, highest = -1e9;
            const auto total = (int) (sampleRate * 2.0);
            const auto tailStart = total - (int) (sampleRate * 0.05);

            for (int i = 0; i < total; ++i)
            {
                pedal.processSample ((float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi
                                                              * 100.0 * i / sampleRate)));
                if (i >= tailStart)
                {
                    const auto reduction = (double) pedal.getCurrentReductionDb();
                    lowest  = std::min (lowest, reduction);
                    highest = std::max (highest, reduction);
                }
            }

            const auto ripple = highest - lowest;
            std::printf ("    release %6.0f ms -> %.2f dB ripple, settling around %.2f dB\n",
                         releaseMs, ripple, highest);

            shrinking = shrinking && ripple < previousRipple;
            previousRipple = ripple;
        }

        test::checkTrue ("ripple shrinks as release lengthens", shrinking);
        test::checkTrue ("ripple is under 0.05 dB at a 1 s release", previousRipple < 0.05);
    }

    // --- Housekeeping ----------------------------------------------------------
    std::puts ("\nHousekeeping:");
    {
        auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 6.0f, 10.0f, 150.0f, 0.0f);
        settledReductionDb (pedal, -2.0);
        pedal.reset();
        test::check ("reset() clears the gain reduction", pedal.getCurrentReductionDb(), 0.0, 1.0e-9);
    }
    {
        auto pedal = makePedal (sampleRate, -18.0f, 4.0f, 6.0f, 10.0f, 150.0f, 12.0f);
        double sum = 0.0;
        bool finite = true;

        for (int i = 0; i < (int) sampleRate; ++i)
        {
            const auto out = pedal.processSample (0.0f);
            sum += std::abs ((double) out);
            finite = finite && std::isfinite (out);
        }

        test::check ("silence in stays silence out", sum, 0.0, 1.0e-12);
        test::checkTrue ("output stays finite through a second of silence", finite);
    }
    {
        // Full-scale slam, then silence: nothing may blow up or stick.
        auto pedal = makePedal (sampleRate, -40.0f, 20.0f, 0.0f, 0.1f, 10.0f, 24.0f);
        bool finite = true;

        for (int i = 0; i < (int) sampleRate; ++i)
            finite = finite && std::isfinite (pedal.processSample (i % 2 == 0 ? 1.0f : -1.0f));

        for (int i = 0; i < (int) sampleRate; ++i)
            finite = finite && std::isfinite (pedal.processSample (0.0f));

        test::checkTrue ("survives full-scale input into silence", finite);
    }

}
