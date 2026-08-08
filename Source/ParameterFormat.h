#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// How a parameter reads to a musician.
//
// Formatting lives on the parameter rather than in the editor, so the
// host's own generic parameter list shows the same thing. A value that
// only reads correctly inside this plugin's window is half-formatted.
//
// The guiding rule: express a value in the unit the musician is actually
// thinking in. A frequency multiplier is an interval. A detection limit
// is a note. A waveshape position is a shape.
namespace ParameterFormat
{
    inline juce::String noteName (float hz)
    {
        if (hz <= 0.0f)
            return {};

        static const char* names[] = { "C","C#","D","D#","E","F",
                                        "F#","G","G#","A","A#","B" };
        const int n = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
        return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
    }

    // Detection limits: the number matters, but so does which note it is,
    // because the setting is chosen against an instrument's range.
    inline juce::String hzWithNote (float hz, int)
    {
        const juce::String value = hz < 1000.0f
            ? juce::String (hz, 0) + " Hz"
            : juce::String (hz / 1000.0f, 2) + " kHz";
        return value + " (" + noteName (hz) + ")";
    }

    // Sync ratio is a frequency multiplier, which is an interval. Two is
    // an octave; a musician has no use for "2.00".
    inline juce::String ratioAsInterval (float ratio, int)
    {
        if (ratio <= 0.0f)
            return "-";

        const float st = 12.0f * std::log2 (ratio);
        const int nearest = juce::roundToInt (st);
        const bool exact = std::abs (st - (float) nearest) < 0.06f;

        juce::String body = exact
            ? (nearest >= 0 ? "+" : "") + juce::String (nearest) + " st"
            : (st >= 0.0f ? "+" : "") + juce::String (st, 1) + " st";

        if (! exact)
            return body;

        switch (nearest)
        {
            case 0:  return body + " (unison)";
            case 3:  return body + " (min 3rd)";
            case 4:  return body + " (maj 3rd)";
            case 5:  return body + " (4th)";
            case 7:  return body + " (5th)";
            case 9:  return body + " (maj 6th)";
            case 12: return body + " (octave)";
            case 16: return body + " (oct+maj3)";
            case 19: return body + " (oct+5th)";
            case 24: return body + " (2 oct)";
            case 31: return body + " (2oct+5th)";
            case 36: return body + " (3 oct)";
            default: return body;
        }
    }

    // Amplitude thresholds. A linear 0-1 figure says nothing about how
    // loud a signal has to be; decibels do.
    inline juce::String decibels (float linear, int)
    {
        if (linear <= 0.0001f)
            return "-inf dB";
        const float db = 20.0f * std::log10 (linear);
        return (db >= 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    inline juce::String milliseconds (float ms, int)
    {
        return ms < 1000.0f ? juce::String (ms, 0) + " ms"
                            : juce::String (ms / 1000.0f, 2) + " s";
    }

    inline juce::String percent (float v, int)
    {
        return juce::String (juce::roundToInt (v * 100.0f)) + " %";
    }

    inline juce::String cutoffHz (float hz, int)
    {
        return hz < 1000.0f ? juce::String (hz, 0) + " Hz"
                            : juce::String (hz / 1000.0f, 2) + " kHz";
    }

    inline juce::String cents (float c, int)
    {
        return juce::String (c, 0) + " ct";
    }

    inline juce::String semitones (float st, int)
    {
        return juce::String (st, 1) + " st";
    }

    inline juce::String octaves (float o, int)
    {
        return juce::String (o, 1) + " oct";
    }

    inline juce::String resonanceQ (float q, int)
    {
        return "Q " + juce::String (q, 2);
    }

    // The morph position names the shape it is sitting on, matching the
    // path the oscillator actually sweeps.
    inline juce::String waveshape (float m, int)
    {
        if (m < 0.16f) return "triangle";
        if (m < 0.42f) return "tri / saw";
        if (m < 0.58f) return "sawtooth";
        if (m < 0.80f) return "square";
        return "narrow pulse";
    }

    // Stability is counted in analysis blocks, which is an implementation
    // detail. The approximate time is the part that means anything, and
    // the tilde is honest about the block size varying with the host.
    inline juce::String stabilityBlocks (int blocks, int)
    {
        return juce::String (blocks) + " (~" + juce::String (blocks * 12) + " ms)";
    }

    inline juce::String notes (int n, int)
    {
        return juce::String (n) + (n == 1 ? " note" : " notes");
    }

    inline juce::String voices (int n, int)
    {
        return juce::String (n) + (n == 1 ? " voice" : " voices");
    }

    inline juce::String noteCount (float v, int)
    {
        const int n = juce::roundToInt (v);
        return juce::String (n) + (n == 1 ? " note" : " notes");
    }

    inline juce::String octaveCount (int n, int)
    {
        return juce::String (n) + (n == 1 ? " octave" : " octaves");
    }

    inline juce::String bpm (float v, int)
    {
        return juce::String (v, 0) + " BPM";
    }

    // Convenience wrappers, so each parameter declaration stays readable.
    inline juce::AudioParameterFloatAttributes floatFmt (juce::String (*fn) (float, int))
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn);
    }

    inline juce::AudioParameterIntAttributes intFmt (juce::String (*fn) (int, int))
    {
        return juce::AudioParameterIntAttributes().withStringFromValueFunction (fn);
    }
}
