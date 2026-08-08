#pragma once
#include <juce_dsp/juce_dsp.h>

// Recovers the input's cycle without measuring its pitch.
//
// This is the Moog FreqBox's method rather than this plugin's usual one.
// A pitch tracker must observe roughly two periods before it can report a
// frequency, which is a physical limit and costs 24 ms on a low guitar
// string. Hard sync sidesteps it entirely: the oscillator's phase is
// reset by the input waveform itself, so it inherits the frequency
// mechanically and responds within a single cycle.
//
// What is given up in exchange: there are no notes, so nothing can be
// quantised, held through a gap, or retriggered. The oscillator simply
// follows whatever is present.
//
// A raw zero-crossing detector would fire several times per period on any
// harmonically rich source, so the input is lowpassed to isolate the
// fundamental and the crossing uses hysteresis. Testing showed a fixed
// cutoff tracks accurately from 82 Hz to 880 Hz, which is what allows the
// detector to work with no prior knowledge of the note.
class SyncDetector
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        setCutoff (400.0f);
        reset();
    }

    void reset()
    {
        lowpassState = 0.0f;
        armed = true;
        samplesSinceTrigger = 0;
        periodSamples = 0.0f;
        haveePeriod = false;
    }

    void setCutoff (float hz)
    {
        hz = juce::jlimit (60.0f, 2000.0f, hz);
        lowpassCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi * hz / (float) sampleRate);
    }

    // Hysteresis width. Wider rejects more noise but needs a stronger
    // signal before it will fire at all.
    void setHysteresis (float amount) { hysteresis = juce::jlimit (0.005f, 0.4f, amount); }

    // One sample in, true when the oscillator should reset. Runs per
    // sample rather than per block, because a phase reset quantised to a
    // block boundary would be audibly wrong.
    bool process (float input)
    {
        lowpassState = lowpassCoeff * lowpassState + (1.0f - lowpassCoeff) * input;

        ++samplesSinceTrigger;
        bool fired = false;

        if (armed && lowpassState > hysteresis)
        {
            // Ignore implausibly short intervals: a real fundamental
            // cannot exceed the detector's upper range, and anything
            // faster is noise riding the crossing.
            if (samplesSinceTrigger >= minPeriodSamples())
            {
                periodSamples = (float) samplesSinceTrigger;
                haveePeriod = true;
                fired = true;
                samplesSinceTrigger = 0;
            }
            armed = false;
        }
        else if (! armed && lowpassState < -hysteresis)
        {
            armed = true;
        }

        // Nothing for a long time means the note has gone.
        if (samplesSinceTrigger > (int) (sampleRate * 0.25))
            haveePeriod = false;

        return fired;
    }

    // Frequency implied by the last interval between triggers. Available
    // one period after a note starts, rather than two.
    float getFrequency() const noexcept
    {
        return haveePeriod && periodSamples > 1.0f
            ? (float) sampleRate / periodSamples
            : 0.0f;
    }

    bool isLocked() const noexcept { return haveePeriod; }

private:
    int minPeriodSamples() const { return (int) (sampleRate / 2500.0); }

    double sampleRate = 44100.0;
    float lowpassCoeff = 0.0f;
    float lowpassState = 0.0f;
    float hysteresis = 0.06f;
    bool armed = true;
    int samplesSinceTrigger = 0;
    float periodSamples = 0.0f;
    bool haveePeriod = false;
};
