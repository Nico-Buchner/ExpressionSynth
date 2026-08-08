#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ModulationState.h"

// Post-synth effects: drive, delay, reverb, in that order.
//
// The parameters worth modulating are exposed as matrix destinations, so
// that the input's expression reaches the effects as it reaches
// everything else. Playing harder adding grit is the same idea as
// playing brighter opening the filter; an effects section that ignored
// the input would be the first part of this plugin that does.
//
// Everything here runs once on the mixed output rather than per voice,
// so the cost does not scale with polyphony.
class EffectsChain
{
public:
    static constexpr auto driveParamID      = "fxDrive";
    static constexpr auto driveToneParamID  = "fxDriveTone";
    static constexpr auto driveLevelParamID = "fxDriveLevel";
    static constexpr auto driveMixParamID   = "fxDriveMix";

    static constexpr auto delayTimeParamID     = "fxDelayTime";
    static constexpr auto delaySyncParamID     = "fxDelaySync";
    static constexpr auto delayDivisionParamID = "fxDelayDiv";
    static constexpr auto delayFeedbackParamID = "fxDelayFb";
    static constexpr auto delayDampParamID     = "fxDelayDamp";
    static constexpr auto delayMixParamID      = "fxDelayMix";

    static constexpr auto reverbSizeParamID    = "fxVerbSize";
    static constexpr auto reverbDampParamID    = "fxVerbDamp";
    static constexpr auto reverbMixParamID     = "fxVerbMix";

    static juce::StringArray getDivisionNames()
    {
        return { "1/4", "1/8", "1/8T", "1/8.", "1/16", "1/16T" };
    }

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params);

    void prepare (double sampleRate, int blockSize, int channels);
    void reset();

    void process (juce::AudioBuffer<float>& buffer,
                   juce::AudioProcessorValueTreeState& params,
                   const ModulationState& modulation,
                   double bpm);

private:
    // Exponential drive curve: gentle across the lower half so the
    // control is usable, steep at the top. A linear curve was already
    // heavily saturated by a fifth of the way up.
    static float driveGain (float amount) { return std::pow (2.0f, amount * 5.0f); }

    static float shape (float x, float amount, float gain)
    {
        float y = std::tanh (x * gain);

        // A little asymmetry adds even harmonics, which reads as warmth
        // rather than the purely odd-harmonic buzz of a symmetric clipper.
        y += 0.10f * amount * (std::tanh (x * gain * 0.7f + 0.3f) - std::tanh (0.3f));
        return y;
    }

    // Normalising against full scale bounds the peak by construction. The
    // level lift at high settings is saturation compressing, not a fault,
    // which is why Level exists as a separate control.
    static float makeupFor (float amount, float gain)
    {
        const float atFullScale = shape (1.0f, amount, gain);
        return atFullScale > 1.0e-6f ? 0.94f / atFullScale : 1.0f;
    }

    double sampleRate = 44100.0;
    int numChannels = 2;

    juce::SmoothedValue<float> driveAmount, driveMix, driveLevel;
    juce::SmoothedValue<float> delayMix, delayFeedback;
    juce::SmoothedValue<float> reverbMix;
    juce::SmoothedValue<float> delaySamples;

    float toneState[2] { 0.0f, 0.0f };
    float dampState[2] { 0.0f, 0.0f };

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 96000 };

    // juce::Reverb, not juce::dsp::Reverb. The dsp wrapper takes a
    // ProcessContext; this one exposes setSampleRate and processStereo,
    // which is what the per-block call below wants.
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;
};
