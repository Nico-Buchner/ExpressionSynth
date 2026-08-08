#pragma once
#include "FeatureExtractor.h"
#include "ModulationState.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Routes analysed features to synth modulation destinations.
//
// Routes are not stored here. Each slot's source, destination, depth and
// curve are read from parameters every block, which makes the matrix
// editable from the interface without any locking: the message thread
// only ever moves parameters, and the audio thread only ever reads them.
// Holding a mutable route list and rebuilding it from the UI would have
// been a data race on the audio thread.
class ExpressionMapper
{
public:
    static constexpr int numSlots = 6;

    // Off is a source rather than a separate enable flag, so a slot has
    // one obvious way to be inactive instead of two.
    enum class Source { Off, Amplitude, Pitch, SpectralCentroid, SpectralFlatness, Onset };
    enum class Destination { FilterCutoff, FilterResonance, Amplitude, PitchBend, OscMorph,
                             DriveAmount, DelayMix, DelayFeedback, ReverbMix };
    enum class Curve { Linear, Exponential, Logarithmic };

    static juce::StringArray getSourceNames()
    {
        return { "Off", "Amplitude", "Pitch", "Brightness", "Noisiness", "Onset" };
    }

    static juce::StringArray getDestinationNames()
    {
        return { "Filter cutoff", "Resonance", "Level", "Pitch bend", "Waveshape",
                 "Drive", "Delay mix", "Delay feedback", "Reverb mix" };
    }

    static juce::StringArray getCurveNames()
    {
        return { "Linear", "Exponential", "Logarithmic" };
    }

    // Parameter IDs are generated so that adding a slot needs no new
    // constants and cannot fall out of step with the layout.
    static juce::String sourceParamID (int slot)      { return "mtxSrc" + juce::String (slot); }
    static juce::String destinationParamID (int slot) { return "mtxDst" + juce::String (slot); }
    static juce::String depthParamID (int slot)       { return "mtxDepth" + juce::String (slot); }
    static juce::String curveParamID (int slot)       { return "mtxCurve" + juce::String (slot); }

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params);

    void prepare (double sampleRate);

    void apply (const FeatureExtractor::Features& features,
                ModulationState& modulation,
                int numSamples,
                juce::AudioProcessorValueTreeState& params);

private:
    static float extractSourceValue (Source source, const FeatureExtractor::Features& f);
    static float shapeCurve (float value, Curve curve);

    juce::SmoothedValue<float> smoothers[numSlots];
    double sampleRate = 44100.0;
};
