#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ArticulationProfile.h"
#include "ModulationState.h"

/**
    Deliberately simple to start: one oscillator + filter + amp envelope
    per voice. The point of v1 is proving the mapping feels expressive,
    not synth complexity — grow this once ExpressionMapper is tuned.

    Parameter handling: APVTS holds the user's base settings (and stays
    normally host-automatable); ModulationState holds live analysis-driven
    modulation. Voices combine the two at render time. See
    ModulationState.h for why modulation is deliberately not written back
    into APVTS.
*/

/** Accepts every note and channel — this synth has one timbre, and
    voice allocation is handled by juce::Synthesiser. Without at least
    one sound registered, Synthesiser::noteOn matches nothing and the
    synth is silent, so this is not optional. */
class SynthSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SynthSound*> (sound) != nullptr;
    }

    // Must be called before any rendering — juce::dsp objects default to
    // a zero sample rate and produce silence or garbage until prepared.
    void prepare (const juce::dsp::ProcessSpec& spec);

    void startNote (int midiNoteNumber, float velocity,
                     juce::SynthesiserSound*, int) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;

    // Called each block before rendering, with the already-combined
    // base+modulation values.
    void updateFromParams (float cutoffHz, float pitchBendSemitones,
                            float filterResonance, float ampGain);

private:
    juce::dsp::Oscillator<float> osc { [] (float x) { return std::sin (x); } };
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::ADSR envelope;
    juce::ADSR::Parameters envParams { 0.01f, 0.1f, 0.8f, 0.3f };

    double baseFrequency = 440.0;
    float currentVelocity = 0.0f;

    // Smoothed so per-block modulation changes don't produce zipper
    // noise at block boundaries.
    juce::SmoothedValue<float> amplitudeGain { 1.0f };

    bool isPrepared = false;
};

class SynthEngine
{
public:
    void prepare (double sampleRate, int samplesPerBlock, int numVoices = 8);

    // --- Base synth parameters (user-set, host-automatable) ---
    static constexpr auto cutoffParamID    = "synthCutoff";
    static constexpr auto resonanceParamID = "synthResonance";
    static constexpr auto ampLevelParamID  = "synthLevel";

    // How far analysis-driven modulation can push the filter, in octaves
    // above the user's base cutoff.
    static constexpr auto cutoffModDepthParamID = "synthCutoffModDepth";

    // --- Articulation detection params ---
    static constexpr auto articulationPresetParamID = "artPreset";
    static constexpr auto onsetThresholdParamID     = "artOnsetThreshold";
    static constexpr auto releaseThresholdParamID   = "artReleaseThreshold";
    static constexpr auto retriggerSensParamID      = "artRetriggerSens";
    static constexpr auto pitchStabilityParamID     = "artPitchStability";
    static constexpr auto glideModeParamID          = "artGlideMode";
    static constexpr auto bendRangeParamID          = "artBendRange";

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void renderNextBlock (juce::AudioBuffer<float>& buffer,
                           const juce::MidiBuffer& midi,
                           juce::AudioProcessorValueTreeState& params,
                           const ModulationState& modulation);

private:
    juce::Synthesiser synth;
    double currentSampleRate = 44100.0;
};
