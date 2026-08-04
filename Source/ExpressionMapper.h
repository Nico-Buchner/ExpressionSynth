#pragma once
#include "FeatureExtractor.h"
#include "ModulationState.h"
#include <juce_audio_basics/juce_audio_basics.h>

/**
    Routes FeatureExtractor output to synth modulation destinations.
    Routes are data, not code, so a routing UI can edit them later
    without restructuring this class.

    Destinations are a typed enum rather than parameter-ID strings: these
    write into ModulationState (see that header for why modulation is
    kept off the host parameter system), so there is no string to match
    against and a typo can't silently produce a dead route.
*/
class ExpressionMapper
{
public:
    enum class Source { Amplitude, Pitch, SpectralCentroid, SpectralFlatness, Onset };
    enum class Destination { FilterCutoff, FilterResonance, Amplitude, PitchBend, OscMorph };
    enum class Curve  { Linear, Exponential, Logarithmic };

    struct Route
    {
        Source source;
        Destination destination;
        Curve curve = Curve::Linear;
        float depth = 1.0f;           // 0-1, how strongly this route applies
        float smoothingMs = 20.0f;    // per-route smoothing, independent of others
        bool enabled = true;
    };

    void prepare (double sampleRate);

    void addRoute (const Route& route);
    void clearRoutes();

    const std::vector<Route>& getRoutes() const noexcept { return routes; }

    // Call once per block after FeatureExtractor::process(). numSamples
    // is required so smoothing advances by a whole block rather than a
    // single sample - otherwise a 20ms smoothing time would take
    // 20ms * blockSize to actually arrive.
    void apply (const FeatureExtractor::Features& features,
                ModulationState& modulation, int numSamples);

private:
    static float extractSourceValue (Source source, const FeatureExtractor::Features& f);
    static float shapeCurve (float value, Curve curve);

    std::vector<Route> routes;

    // One smoothed value per route, indexed the same way as `routes`.
    std::vector<juce::SmoothedValue<float>> smoothers;

    double sampleRate = 44100.0;
};
