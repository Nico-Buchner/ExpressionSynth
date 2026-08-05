#include "SynthEngine.h"

void SynthVoice::prepare (const juce::dsp::ProcessSpec& spec)
{
    juce::dsp::ProcessSpec stereoSpec = spec;
    stereoSpec.numChannels = 2;

    for (auto& osc : oscs)
        osc.prepare (spec.sampleRate);

    filter.prepare (stereoSpec);
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    envelope.setSampleRate (spec.sampleRate);
    outputGain.reset (spec.sampleRate, 0.02);

    refreshUnison (1, 0.0f, 0.0f);
    isPrepared = true;
}

void SynthVoice::refreshUnison (int count, float detuneCents, float spread)
{
    activeUnison = juce::jlimit (1, maxUnison, count);

    for (int u = 0; u < activeUnison; ++u)
    {
        // Position within the stack, -1 to +1.
        const float pos = activeUnison > 1
            ? (2.0f * (float) u / (float) (activeUnison - 1)) - 1.0f
            : 0.0f;

        detuneRatio[u] = std::pow (2.0f, (pos * detuneCents) / 1200.0f);

        // Equal-power pan so the stack keeps a constant level as it widens.
        const float angle = (pos * spread + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        panL[u] = std::cos (angle);
        panR[u] = std::sin (angle);
    }

    // Detuned oscillators are only partly correlated, so summing n of
    // them raises level by roughly sqrt(n) rather than n.
    unisonScale = 1.0f / std::sqrt ((float) activeUnison);

    cachedDetune = detuneCents;
    cachedSpread = spread;
}

void SynthVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    baseFrequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
    currentVelocity = velocity;

    // Stagger the starting phases. Without this the stack begins fully
    // in phase, which both sounds like a single oscillator for the first
    // few milliseconds and produces a transient peak of up to sqrt(n).
    for (int u = 0; u < maxUnison; ++u)
    {
        oscs[u].reset();
        oscs[u].setPhaseOffset ((float) u / (float) maxUnison);
    }

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

void SynthVoice::updateFromParams (const VoiceParams& p)
{
    if (! isPrepared)
        return;

    if (p.unisonCount != activeUnison
        || std::abs (p.detuneCents - cachedDetune) > 0.01f
        || std::abs (p.spread - cachedSpread) > 0.001f)
    {
        refreshUnison (p.unisonCount, p.detuneCents, p.spread);
    }

    const float bent = (float) baseFrequency * std::pow (2.0f, p.pitchBendSemitones / 12.0f);

    for (int u = 0; u < activeUnison; ++u)
    {
        oscs[u].setFrequency (juce::jlimit (20.0f, 20000.0f, bent * detuneRatio[u]));
        oscs[u].setMorph (p.morph);
    }

    filter.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, p.cutoffHz));
    filter.setResonance (juce::jlimit (0.1f, 10.0f, p.resonance));

    envelope.setParameters (p.adsr);
    outputGain.setTargetValue (juce::jlimit (0.0f, 1.0f, p.gain));
}

void SynthVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (! isPrepared || ! isVoiceActive())
        return;

    const int numChannels = outputBuffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        float left = 0.0f;
        float right = 0.0f;

        for (int u = 0; u < activeUnison; ++u)
        {
            const float s = oscs[u].processSample();
            left  += s * panL[u];
            right += s * panR[u];
        }

        left  *= unisonScale;
        right *= unisonScale;

        left  = filter.processSample (0, left);
        right = filter.processSample (1, right);

        const float env = envelope.getNextSample() * currentVelocity * outputGain.getNextValue();
        left  *= env;
        right *= env;

        if (numChannels >= 2)
        {
            outputBuffer.addSample (0, startSample + i, left);
            outputBuffer.addSample (1, startSample + i, right);

            for (int ch = 2; ch < numChannels; ++ch)
                outputBuffer.addSample (ch, startSample + i, (left + right) * 0.5f);
        }
        else if (numChannels == 1)
        {
            outputBuffer.addSample (0, startSample + i, (left + right) * 0.5f);
        }
    }

    if (! envelope.isActive())
    {
        envelope.reset();
        clearCurrentNote();
    }
}

void SynthEngine::prepare (double sampleRate, int samplesPerBlock, int numVoices)
{
    currentSampleRate = sampleRate;

    synth.clearVoices();
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice());

    // Without a registered sound, Synthesiser::noteOn matches nothing
    // and the synth is silent while compiling perfectly well.
    synth.clearSounds();
    synth.addSound (new SynthSound());

    synth.setCurrentPlaybackSampleRate (sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
    spec.numChannels = 1;

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            voice->prepare (spec);
}

juce::AudioProcessorValueTreeState::ParameterLayout SynthEngine::createParameterLayout()
{
    using Param = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { morphParamID, 1 }, "Waveshape",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { unisonParamID, 1 }, "Unison Voices", 1, 4, 1));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { detuneParamID, 1 }, "Unison Detune (cents)",
        juce::NormalisableRange<float> (0.0f, 50.0f), 12.0f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { spreadParamID, 1 }, "Unison Spread",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { cutoffParamID, 1 }, "Filter Cutoff",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 1200.0f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { resonanceParamID, 1 }, "Filter Resonance",
        juce::NormalisableRange<float> (0.1f, 10.0f), 0.7f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { cutoffModDepthParamID, 1 }, "Cutoff Mod Depth (octaves)",
        juce::NormalisableRange<float> (0.0f, 6.0f), 4.0f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { morphModDepthParamID, 1 }, "Waveshape Mod Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { attackParamID, 1 }, "Attack (ms)",
        juce::NormalisableRange<float> (1.0f, 2000.0f, 0.1f, 0.3f), 10.0f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { decayParamID, 1 }, "Decay (ms)",
        juce::NormalisableRange<float> (1.0f, 2000.0f, 0.1f, 0.3f), 150.0f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { sustainParamID, 1 }, "Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { releaseParamID, 1 }, "Release (ms)",
        juce::NormalisableRange<float> (1.0f, 4000.0f, 0.1f, 0.3f), 300.0f));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { ampLevelParamID, 1 }, "Output Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { adaptiveParamID, 1 }, "Adaptive", false));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { adaptRateParamID, 1 }, "Adapt Over (notes)",
        juce::NormalisableRange<float> (1.0f, 32.0f, 1.0f), 4.0f));

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

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { confidenceGateParamID, 1 }, "Confidence Gate",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { pitchStabilityParamID, 1 }, "Pitch Stability (blocks)", 1, 12, 3));

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
    VoiceParams vp;

    const float baseCutoff = params.getRawParameterValue (cutoffParamID)->load();
    const float modOctaves = params.getRawParameterValue (cutoffModDepthParamID)->load();
    const float cutoffMod  = modulation.filterCutoff.load();

    // Filter modulation applies in octaves above the base cutoff. A fixed
    // Hz offset is a huge move low down and inaudible high up, because
    // pitch perception is logarithmic.
    vp.cutoffHz = juce::jlimit (20.0f, 20000.0f,
                                 baseCutoff * std::pow (2.0f, cutoffMod * modOctaves));

    vp.resonance = juce::jlimit (0.1f, 10.0f,
                                  params.getRawParameterValue (resonanceParamID)->load()
                                  + modulation.filterResonance.load() * 5.0f);

    vp.pitchBendSemitones = modulation.pitchBendSemitones.load();
    // Base waveshape plus analysis-driven offset. A bright input pushes
    // the oscillator toward saw and square; a dull one falls back to
    // triangle, so output timbre tracks input timbre directly.
    vp.morph = juce::jlimit (0.0f, 1.0f,
                              params.getRawParameterValue (morphParamID)->load()
                              + modulation.oscMorph.load()
                                * params.getRawParameterValue (morphModDepthParamID)->load());
    vp.unisonCount = (int) params.getRawParameterValue (unisonParamID)->load();
    vp.detuneCents = params.getRawParameterValue (detuneParamID)->load();
    vp.spread      = params.getRawParameterValue (spreadParamID)->load();

    vp.gain = params.getRawParameterValue (ampLevelParamID)->load()
                * modulation.amplitude.load();

    vp.adsr.attack  = params.getRawParameterValue (attackParamID)->load()  * 0.001f;
    vp.adsr.decay   = params.getRawParameterValue (decayParamID)->load()   * 0.001f;
    vp.adsr.sustain = params.getRawParameterValue (sustainParamID)->load();
    vp.adsr.release = params.getRawParameterValue (releaseParamID)->load() * 0.001f;

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            voice->updateFromParams (vp);

    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
}
