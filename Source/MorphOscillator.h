#pragma once
#include <juce_dsp/juce_dsp.h>

// An oscillator whose waveshape sweeps continuously rather than
// switching, following the Moog MF-107 FreqBox path: triangle, through
// sawtooth, to square, ending at a narrow pulse.
//
// Square and pulse are one generator at different widths, so the last
// third of the sweep narrows the pulse rather than crossfading - which
// is what the hardware does.
//
// Three corrections that testing showed were necessary:
//  - Saw and pulse use PolyBLEP. A naive saw aliases badly, and with
//    fundamentals reaching 2 kHz the folded partials are audible.
//  - The pulse has its analytic DC removed. A narrow pulse otherwise
//    sits near -1 for most of its cycle, which wastes headroom.
//  - The pulse's phase reference is shifted half a cycle so it
//    correlates positively with the saw. Without this the crossfade
//    between them partially cancels and the level dips mid-sweep.
class MorphOscillator
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        reset();
        setMorph (morph);
    }

    void reset()
    {
        phase = 0.0f;
    }

    // Offsets the phase without changing frequency. Unison voices use
    // this so the stack does not begin fully in phase.
    void setPhaseOffset (float offset)
    {
        phase = offset - std::floor (offset);
    }

    // Hard sync: the input waveform resets the cycle. The discontinuity
    // this creates IS the sound - smoothing it away would remove the
    // character - so it is left sharp. It does alias, as hardware sync
    // does; see the manual.
    void sync() { phase = 0.0f; }

    void setFrequency (float hz)
    {
        hz = juce::jlimit (0.0f, (float) sampleRate * 0.45f, hz);
        phaseIncrement = hz / (float) sampleRate;
    }

    // Crossfade gains are derived here rather than per sample, so the
    // square roots are paid once per block instead of once per sample.
    void setMorph (float position)
    {
        morph = juce::jlimit (0.0f, 1.0f, position);

        if (morph < oneThird)
        {
            region = 0;
            const float k = morph * 3.0f;
            // Triangle and saw are uncorrelated, so a linear crossfade
            // dips in the middle. Equal-power holds the level flat.
            gainA = std::sqrt (1.0f - k);
            gainB = std::sqrt (k);
        }
        else if (morph < twoThirds)
        {
            region = 1;
            const float k = (morph - oneThird) * 3.0f;
            // Phase-aligned above, so these add rather than cancel and a
            // linear crossfade is correct here.
            gainA = 1.0f - k;
            gainB = k;
        }
        else
        {
            region = 2;
            const float k = (morph - twoThirds) * 3.0f;
            pulseWidth = 0.5f - (0.5f - minPulseWidth) * k;
        }
    }

    float processSample()
    {
        const float dt = phaseIncrement;
        float out = 0.0f;

        switch (region)
        {
            case 0:  out = triangle() * gainA + saw (dt) * gainB;            break;
            case 1:  out = saw (dt) * gainA + pulse (dt, 0.5f) * gainB;      break;
            default: out = pulse (dt, pulseWidth);                           break;
        }

        phase += dt;
        if (phase >= 1.0f)
            phase -= 1.0f;

        return out * outputScale;
    }

private:
    static constexpr float oneThird = 1.0f / 3.0f;
    static constexpr float twoThirds = 2.0f / 3.0f;
    static constexpr float minPulseWidth = 0.12f;

    // Equal-power crossfade can sum to 1.41x when both waveforms align,
    // so scale down to keep the worst case within unity.
    static constexpr float outputScale = 0.72f;

    // Polynomial band-limited step: smooths the one-sample jump that
    // causes aliasing, using the fractional distance to the edge.
    static float polyBlep (float t, float dt)
    {
        if (dt <= 0.0f)
            return 0.0f;

        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }

        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }

        return 0.0f;
    }

    // Triangle has no value discontinuity, only a slope one, so its
    // partials fall away fast enough to leave naive.
    float triangle() const
    {
        return 4.0f * std::abs (phase - 0.5f) - 1.0f;
    }

    float saw (float dt) const
    {
        return (2.0f * phase - 1.0f) - polyBlep (phase, dt);
    }

    float pulse (float dt, float width) const
    {
        width = juce::jlimit (0.01f, 0.99f, width);

        float p = phase + 0.5f;
        if (p >= 1.0f)
            p -= 1.0f;

        float value = p < width ? 1.0f : -1.0f;
        value += polyBlep (p, dt);

        float edge = p - width;
        if (edge < 0.0f)
            edge += 1.0f;
        value -= polyBlep (edge, dt);

        value -= (2.0f * width - 1.0f);   // remove analytic DC
        value /= (2.0f - 2.0f * width);   // normalise peak to 1
        value *= 0.577f;                  // match saw/triangle RMS at 50%

        return value;
    }

    double sampleRate = 44100.0;
    float phase = 0.0f;
    float phaseIncrement = 0.0f;
    float morph = 0.0f;

    int region = 0;
    float gainA = 1.0f;
    float gainB = 0.0f;
    float pulseWidth = 0.5f;
};
