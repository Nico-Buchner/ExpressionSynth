#include "SynthEngine.h"

// ---------- SynthVoice ----------

void SynthVoice::prepare (const juce::dsp::ProcessSpec& spec)
{
    osc.prepare (spec);
    filter.prepare (spec);
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    envelope.setSampleRate (spec.sampleRate);
    envelope.setParameters (envParams);

    amplitudeGain.reset (spec.sampleRate, 0.02);
    amplitudeGain.setCurrentAndTargetValue (1.0f);

    isPrepared = true;
}

void SynthVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    baseFrequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
    currentVelocity = velocity;
    osc.setFrequency ((float) baseFrequency, true); // force, so the new note starts in tune
    envelope.setParameters (envParams);
    envelope.noteOn();
}

void SynthVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        envelope.noteOff();
    }
    else
    {
        envelope.reset();
        clearCurrentNote();
    }
}

void SynthVoice::updateFromParams (float cutoffHz, float pitchBendSemitones,
                                    float filterResonance, float ampGain)
{
    if (! isPrepared)
        return;

    const float bentFreq = (float) baseFrequency * std::pow (2.0f, pitchBendSemitones / 12.0f);
    osc.setFrequency (juce::jlimit (20.0f, 20000.0f, bentFreq));
    filter.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, cutoffHz));
    filter.setResonance (juce::jlimit (0.1f, 10.0f, filterResonance));
    amplitudeGain.setTargetValue (juce::jlimit (0.0f, 1.0f, ampGain));
}

void SynthVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    // Guard: rendering before prepare() would read a zero sample rate.
    if (! isPrepared || ! isVoiceActive())
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = osc.processSample (0.0f);
        sample = filter.processSample (0, sample);
        sample *= envelope.getNextSample()
                    * currentVelocity
                    * amplitudeGain.getNextValue();

        for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
            outputBuffer.addSample (ch, startSample + i, sample);
    }

    if (! envelope.isActive())
    {
        envelope.reset();
        clearCurrentNote();
    }
}

// ---------- SynthEngine ----------

void SynthEngine::prepare (double sampleRate, int samplesPerBlock, int numVoices)
{
    currentSampleRate = sampleRate;

    synth.clearVoices();
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice());

    // Without at least one registered sound, Synthesiser::noteOn finds
    // nothing to play and the synth is completely silent.
    synth.clearSounds();
    synth.addSound (new SynthSound());

    synth.setCurrentPlaybackSampleRate (sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
    spec.numChannels = 1; // voices render mono, then fan out to all output channels

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            voice->prepare (spec);
}

juce::AudioProcessorValueTreeState::ParameterLayout SynthEngine::createParameterLayout()
{
    using Param = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // --- Base synth settings ---
    params.push_back (std::make_unique<Param> (
        juce::ParameterID { cutoffParamID, 1 }, "Filter Cutoff",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 800.0f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { resonanceParamID, 1 }, "Filter Resonance",
        juce::NormalisableRange<float> (0.1f, 10.0f), 0.7f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { ampLevelParamID, 1 }, "Output Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { cutoffModDepthParamID, 1 }, "Cutoff Mod Depth (octaves)",
        juce::NormalisableRange<float> (0.0f, 6.0f), 4.0f));

    // --- Articulation detection ---
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { articulationPresetParamID, 1 }, "Articulation",
        ArticulationProfile::getPresetNames(), 0));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { onsetThresholdParamID, 1 }, "Onset Threshold",
        juce::NormalisableRange<float> (0.005f, 0.30f), 0.08f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { releaseThresholdParamID, 1 }, "Release Threshold",
        juce::NormalisableRange<float> (0.002f, 0.20f), 0.015f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { retriggerSensParamID, 1 }, "Retrigger Threshold",
        juce::NormalisableRange<float> (0.05f, 1.0f), 0.30f));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { pitchStabilityParamID, 1 }, "Pitch Stability (blocks)",
        1, 12, 3));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { glideModeParamID, 1 }, "Glide Mode", false));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { bendRangeParamID, 1 }, "Bend Range (semitones)",
        juce::NormalisableRange<float> (0.5f, 12.0f), 2.0f));

    return { params.begin(), params.end() };
}

void SynthEngine::renderNextBlock (juce::AudioBuffer<float>& buffer,
                                    const juce::MidiBuffer& midi,
                                    juce::AudioProcessorValueTreeState& params,
                                    const ModulationState& modulation)
{
    // User's base settings.
    const float baseCutoff = params.getRawParameterValue (cutoffParamID)->load();
    const float baseRes    = params.getRawParameterValue (resonanceParamID)->load();
    const float level      = params.getRawParameterValue (ampLevelParamID)->load();
    const float modOctaves = params.getRawParameterValue (cutoffModDepthParamID)->load();

    // Live modulation from analysis.
    const float cutoffMod = modulation.filterCutoff.load();
    const float resMod    = modulation.filterResonance.load();
    const float ampMod    = modulation.amplitude.load();
    const float bend      = modulation.pitchBendSemitones.load();

    // Filter modulation is applied in octaves above the base cutoff —
    // linear Hz modulation sounds wrong because pitch perception is
    // logarithmic, so a fixed Hz offset is a huge move down low and
    // an inaudible one up high.
    const float cutoff = juce::jlimit (20.0f, 20000.0f,
                                        baseCutoff * std::pow (2.0f, cutoffMod * modOctaves));
    const float resonance = juce::jlimit (0.1f, 10.0f, baseRes + resMod * 5.0f);
    const float gain = level * ampMod;

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            voice->updateFromParams (cutoff, bend, resonance, gain);

    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
}
