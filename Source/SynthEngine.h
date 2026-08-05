#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ArticulationProfile.h"
#include "ModulationState.h"
#include "MorphOscillator.h"
#include "SyncDetector.h"

class SynthSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

// Everything a voice needs for one block, gathered once rather than
// passed as a long argument list.
struct VoiceParams
{
    float cutoffHz = 800.0f;
    float resonance = 0.7f;
    float pitchBendSemitones = 0.0f;
    float morph = 0.0f;
    int unisonCount = 1;
    float detuneCents = 0.0f;
    float spread = 0.0f;
    float gain = 0.8f;
    juce::ADSR::Parameters adsr { 0.01f, 0.15f, 0.8f, 0.3f };
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SynthSound*> (sound) != nullptr;
    }

    void prepare (const juce::dsp::ProcessSpec& spec);

    void startNote (int midiNoteNumber, float velocity,
                     juce::SynthesiserSound*, int) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;

    void updateFromParams (const VoiceParams& p);

private:
    void refreshUnison (int count, float detuneCents, float spread);

    static constexpr int maxUnison = 4;

    MorphOscillator oscs[maxUnison];
    float detuneRatio[maxUnison] { 1.0f, 1.0f, 1.0f, 1.0f };
    float panL[maxUnison] { 1.0f, 1.0f, 1.0f, 1.0f };
    float panR[maxUnison] { 1.0f, 1.0f, 1.0f, 1.0f };

    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::ADSR envelope;

    double baseFrequency = 440.0;
    float currentVelocity = 0.0f;

    int activeUnison = 1;
    float unisonScale = 1.0f;
    float cachedDetune = -1.0f;
    float cachedSpread = -1.0f;

    juce::SmoothedValue<float> outputGain { 0.8f };

    bool isPrepared = false;
};

// Hard-sync voice. Deliberately not a juce::SynthesiserVoice: there are
// no note events in sync mode, so there is nothing to allocate voices
// for. The oscillators run continuously and the input's own envelope
// opens and closes the output.
class SyncVoice
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setParams (const VoiceParams& p, float ratio, float envReleaseMs, float hysteresis);

    // Renders straight from the mono input, replacing the buffer.
    void render (juce::AudioBuffer<float>& output,
                  const float* monoInput, int numSamples);

    float getTrackedFrequency() const noexcept { return detector.getFrequency(); }
    bool isLocked() const noexcept { return detector.isLocked(); }

private:
    static constexpr int maxUnison = 4;

    SyncDetector detector;
    MorphOscillator oscs[maxUnison];
    float detuneRatio[maxUnison] { 1.0f, 1.0f, 1.0f, 1.0f };
    float panL[maxUnison] { 1.0f, 1.0f, 1.0f, 1.0f };
    float panR[maxUnison] { 1.0f, 1.0f, 1.0f, 1.0f };

    juce::dsp::StateVariableTPTFilter<float> filter;

    int activeUnison = 1;
    float unisonScale = 1.0f;
    float cachedDetune = -1.0f, cachedSpread = -1.0f;

    float syncRatio = 1.0f;
    float gain = 0.8f;

    // Fast per-sample follower. The block-rate envelope used elsewhere
    // would undo the point of running sample-accurate sync.
    float envelope = 0.0f;
    float envAttack = 0.0f, envRelease = 0.0f;

    double sampleRate = 44100.0;
    bool isPrepared = false;
};

class SynthEngine
{
public:
    void prepare (double sampleRate, int samplesPerBlock, int numVoices = 8);

    static constexpr auto cutoffParamID    = "synthCutoff";
    static constexpr auto resonanceParamID = "synthResonance";
    static constexpr auto ampLevelParamID  = "synthLevel";
    static constexpr auto cutoffModDepthParamID = "synthCutoffModDepth";
    static constexpr auto morphModDepthParamID  = "synthMorphModDepth";

    static constexpr auto morphParamID   = "synthMorph";
    static constexpr auto unisonParamID  = "synthUnison";
    static constexpr auto detuneParamID  = "synthDetune";
    static constexpr auto spreadParamID  = "synthSpread";

    static constexpr auto attackParamID  = "synthAttack";
    static constexpr auto decayParamID   = "synthDecay";
    static constexpr auto sustainParamID = "synthSustain";
    static constexpr auto releaseParamID = "synthRelease";

    static constexpr auto articulationPresetParamID = "artPreset";
    static constexpr auto onsetThresholdParamID     = "artOnsetThreshold";
    static constexpr auto releaseThresholdParamID   = "artReleaseThreshold";
    static constexpr auto retriggerSensParamID      = "artRetriggerSens";
    static constexpr auto confidenceGateParamID     = "artConfidenceGate";
    static constexpr auto pitchStabilityParamID     = "artPitchStability";
    static constexpr auto glideModeParamID          = "artGlideMode";
    static constexpr auto bendRangeParamID          = "artBendRange";
    static constexpr auto adaptiveParamID           = "artAdaptive";
    static constexpr auto adaptRateParamID          = "artAdaptRate";
    static constexpr auto syncModeParamID           = "engSyncMode";
    static constexpr auto syncRatioParamID          = "engSyncRatio";
    static constexpr auto syncSensParamID           = "engSyncSens";
    static constexpr auto syncReleaseParamID        = "engSyncRelease";
    static constexpr auto pitchMinParamID           = "detPitchMin";
    static constexpr auto pitchMaxParamID           = "detPitchMax";

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void renderNextBlock (juce::AudioBuffer<float>& buffer,
                           const juce::MidiBuffer& midi,
                           juce::AudioProcessorValueTreeState& params,
                           const ModulationState& modulation,
                           const float* monoInput);

    bool isSyncMode() const noexcept { return syncMode; }
    float getSyncFrequency() const noexcept { return syncVoice.getTrackedFrequency(); }
    bool isSyncLocked() const noexcept { return syncVoice.isLocked(); }

private:
    VoiceParams gatherParams (juce::AudioProcessorValueTreeState& params,
                               const ModulationState& modulation) const;

    juce::Synthesiser synth;
    SyncVoice syncVoice;
    bool syncMode = false;
    double currentSampleRate = 44100.0;
};
