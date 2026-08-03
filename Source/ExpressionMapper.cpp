#include "ExpressionMapper.h"

void ExpressionMapper::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    for (size_t i = 0; i < routes.size(); ++i)
    {
        smoothers[i].reset (sampleRate, routes[i].smoothingMs * 0.001);
        smoothers[i].setCurrentAndTargetValue (0.0f);
    }
}

void ExpressionMapper::addRoute (const Route& route)
{
    routes.push_back (route);
    juce::SmoothedValue<float> smoother;
    smoother.reset (sampleRate, route.smoothingMs * 0.001);
    smoother.setCurrentAndTargetValue (0.0f);
    smoothers.push_back (smoother);
}

void ExpressionMapper::clearRoutes()
{
    routes.clear();
    smoothers.clear();
}

void ExpressionMapper::apply (const FeatureExtractor::Features& features,
                               ModulationState& modulation, int numSamples)
{
    for (size_t i = 0; i < routes.size(); ++i)
    {
        const auto& route = routes[i];
        if (! route.enabled)
            continue;

        const float raw = extractSourceValue (route.source, features);
        const float shaped = shapeCurve (raw, route.curve) * route.depth;

        smoothers[i].setTargetValue (shaped);

        // Advance a whole block, not one sample — smoothing times are
        // specified in real milliseconds and must behave that way
        // regardless of block size.
        smoothers[i].skip (juce::jmax (0, numSamples - 1));
        const float smoothed = smoothers[i].getNextValue();

        switch (route.destination)
        {
            case Destination::FilterCutoff:    modulation.filterCutoff.store (smoothed); break;
            case Destination::FilterResonance: modulation.filterResonance.store (smoothed); break;
            case Destination::Amplitude:       modulation.amplitude.store (smoothed); break;
            case Destination::PitchBend:       modulation.pitchBendSemitones.store (smoothed); break;
        }
    }
}

float ExpressionMapper::extractSourceValue (Source source, const FeatureExtractor::Features& f)
{
    switch (source)
    {
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
