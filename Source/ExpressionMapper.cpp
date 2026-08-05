#include "ExpressionMapper.h"

void ExpressionMapper::addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params)
{
    // Defaults reproduce the routing the plugin shipped with before the
    // matrix became editable, so an existing patch behaves the same.
    struct Preset { int source, destination, curve; float depth; };
    const Preset defaults[numSlots] =
    {
        { 1, 2, 0, 1.0f },   // Amplitude  -> Level
        { 3, 4, 0, 1.0f },   // Brightness -> Waveshape
        { 3, 0, 1, 1.0f },   // Brightness -> Filter cutoff (exponential)
        { 0, 1, 0, 0.5f },   // off
        { 0, 0, 0, 0.5f },   // off
        { 0, 0, 0, 0.5f }    // off
    };

    for (int i = 0; i < numSlots; ++i)
    {
        const auto n = juce::String (i + 1);

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { sourceParamID (i), 1 }, "Route " + n + " Source",
            getSourceNames(), defaults[i].source));

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { destinationParamID (i), 1 }, "Route " + n + " Destination",
            getDestinationNames(), defaults[i].destination));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { depthParamID (i), 1 }, "Route " + n + " Depth",
            juce::NormalisableRange<float> (0.0f, 1.0f), defaults[i].depth));

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { curveParamID (i), 1 }, "Route " + n + " Curve",
            getCurveNames(), defaults[i].curve));
    }
}

void ExpressionMapper::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    for (auto& s : smoothers)
    {
        s.reset (sampleRate, 0.025);
        s.setCurrentAndTargetValue (0.0f);
    }
}

void ExpressionMapper::apply (const FeatureExtractor::Features& features,
                               ModulationState& modulation,
                               int numSamples,
                               juce::AudioProcessorValueTreeState& params)
{
    float sums[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    bool written[5] = { false, false, false, false, false };

    for (int slot = 0; slot < numSlots; ++slot)
    {
        const auto source = (Source) (int) params.getRawParameterValue (sourceParamID (slot))->load();

        // A slot set to Off still advances its smoother, so re-enabling
        // it does not jump from a stale value.
        const float raw = source == Source::Off
            ? 0.0f
            : extractSourceValue (source, features);

        const auto curve = (Curve) (int) params.getRawParameterValue (curveParamID (slot))->load();
        const float depth = params.getRawParameterValue (depthParamID (slot))->load();

        smoothers[slot].setTargetValue (shapeCurve (raw, curve) * depth);
        smoothers[slot].skip (juce::jmax (0, numSamples - 1));
        const float value = smoothers[slot].getNextValue();

        if (source == Source::Off)
            continue;

        const int dest = juce::jlimit (0, 4,
            (int) params.getRawParameterValue (destinationParamID (slot))->load());

        // Several slots may target one destination; they add.
        sums[dest] += value;
        written[dest] = true;
    }

    modulation.filterCutoff.store    (juce::jlimit (0.0f, 1.0f, sums[0]));
    modulation.filterResonance.store (juce::jlimit (0.0f, 1.0f, sums[1]));

    // An unrouted level leaves the synth at its own volume rather than
    // silencing it, which is what a neutral value means here.
    modulation.amplitude.store (written[2] ? juce::jlimit (0.0f, 1.0f, sums[2]) : 1.0f);

    modulation.oscMorph.store (juce::jlimit (0.0f, 1.0f, sums[4]));

    // Pitch bend is written by PluginProcessor from the detected note's
    // cents deviation. A slot targeting it adds to that rather than
    // replacing it, so the note stays in tune when nothing is routed.
    if (written[3])
        modulation.pitchBendSemitones.store (
            modulation.pitchBendSemitones.load() + sums[3] * 12.0f);
}

float ExpressionMapper::extractSourceValue (Source source, const FeatureExtractor::Features& f)
{
    switch (source)
    {
        case Source::Off:              return 0.0f;
        case Source::Amplitude:        return f.amplitude;
        case Source::Pitch:            return f.pitchConfidence > 0.5f
                                            ? juce::jlimit (0.0f, 1.0f, f.pitchHz / 2000.0f)
                                            : 0.0f;
        case Source::SpectralCentroid: return f.spectralCentroid;
        case Source::SpectralFlatness: return f.spectralFlatness;
        case Source::Onset:            return f.onsetStrength;
    }
    return 0.0f;
}

float ExpressionMapper::shapeCurve (float value, Curve curve)
{
    switch (curve)
    {
        case Curve::Linear:      return value;
        case Curve::Exponential: return value * value;
        case Curve::Logarithmic: return std::sqrt (value);
    }
    return value;
}
