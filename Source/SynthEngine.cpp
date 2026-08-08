#include "SynthEngine.h"
#include "ExpressionMapper.h"
#include "Arpeggiator.h"
#include "EffectsChain.h"
#include "ParameterFormat.h"

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

    isPrepared = true;
}

void SynthVoice::configureOscillators (const VoiceParams& p)
{
    activeOscs = 0;
    float levelSum = 0.0f;

    if (p.stackMode)
    {
        // Each oscillator is its own voice in the chord sense: its own
        // waveform, its own octave, its own place in the stereo field.
        for (int i = 0; i < maxUnison; ++i)
        {
            const auto& slot = p.slots[i];
            if (! slot.active)
                continue;

            const int k = activeOscs++;
            oscs[k].setShape (slot.shape);
            freqRatio[k] = std::pow (2.0f, (float) slot.octave + slot.detuneCents / 1200.0f);
            oscLevel[k] = slot.level;

            const float angle = (slot.pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            panL[k] = std::cos (angle);
            panR[k] = std::sin (angle);
            levelSum += slot.level;
        }
    }
    else
    {
        // One waveform detuned against itself, spread across the field.
        const int count = juce::jlimit (1, maxUnison, p.unisonCount);

        for (int i = 0; i < count; ++i)
        {
            const float pos = count > 1
                ? (2.0f * (float) i / (float) (count - 1)) - 1.0f
                : 0.0f;

            oscs[i].setShape (MorphOscillator::Shape::Morph);
            oscs[i].setMorph (p.morph);
            freqRatio[i] = std::pow (2.0f, (pos * p.detuneCents) / 1200.0f);
            oscLevel[i] = 1.0f;

            const float angle = (pos * p.spread + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            panL[i] = std::cos (angle);
            panR[i] = std::sin (angle);
            levelSum += 1.0f;
        }
        activeOscs = count;
    }

    // Detuned oscillators are only partly correlated, so their sum grows
    // roughly with the square root of the total rather than linearly.
    mixScale = levelSum > 0.0f ? 1.0f / std::sqrt (levelSum) : 0.0f;
}

void SynthVoice::startNote (int midiNoteNumber, float velocity,
                             juce::SynthesiserSound* sound, int)
{
    // Which sound matched tells us the channel the note arrived on, and
    // so whether the player produced it with an instrument or a keyboard.
    audioTriggered = dynamic_cast<AudioOriginSound*> (sound) != nullptr;

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

    configureOscillators (p);

    const float bendSemis = audioTriggered ? p.pitchBendSemitones : 0.0f;
    const float bent = (float) baseFrequency * std::pow (2.0f, bendSemis / 12.0f);

    for (int i = 0; i < activeOscs; ++i)
        oscs[i].setFrequency (juce::jlimit (20.0f, 20000.0f, bent * freqRatio[i]));

    filter.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, p.cutoffHz));
    filter.setResonance (juce::jlimit (0.1f, 10.0f, p.resonance));

    envelope.setParameters (p.adsr);
    outputGain.setTargetValue (juce::jlimit (0.0f, 1.0f,
                                              audioTriggered ? p.envelopedGain : p.plainGain));
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

        for (int u = 0; u < activeOscs; ++u)
        {
            const float s = oscs[u].processSample() * oscLevel[u];
            left  += s * panL[u];
            right += s * panR[u];
        }

        left  *= mixScale;
        right *= mixScale;

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

// ---------- SyncVoice ----------

void SyncVoice::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    detector.prepare (spec.sampleRate);

    for (auto& o : oscs)
        o.prepare (spec.sampleRate);

    juce::dsp::ProcessSpec stereo = spec;
    stereo.numChannels = 2;
    filter.prepare (stereo);
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    // 1 ms attack: fast enough that the output opens with the note rather
    // than after it.
    envAttack = std::exp (-1.0f / (float) (0.001 * spec.sampleRate));
    envRelease = std::exp (-1.0f / (float) (0.080 * spec.sampleRate));

    isPrepared = true;
    reset();
}

void SyncVoice::reset()
{
    detector.reset();
    envelope = 0.0f;
    for (int u = 0; u < maxUnison; ++u)
    {
        oscs[u].reset();
        oscs[u].setPhaseOffset ((float) u / (float) maxUnison);
    }
}

void SyncVoice::setParams (const VoiceParams& p, float ratio, float envReleaseMs, float hysteresis)
{
    if (! isPrepared)
        return;

    if (p.unisonCount != activeUnison
        || std::abs (p.detuneCents - cachedDetune) > 0.01f
        || std::abs (p.spread - cachedSpread) > 0.001f)
    {
        activeUnison = juce::jlimit (1, maxUnison, p.unisonCount);

        for (int u = 0; u < activeUnison; ++u)
        {
            const float pos = activeUnison > 1
                ? (2.0f * (float) u / (float) (activeUnison - 1)) - 1.0f : 0.0f;

            detuneRatio[u] = std::pow (2.0f, (pos * p.detuneCents) / 1200.0f);

            const float angle = (pos * p.spread + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            panL[u] = std::cos (angle);
            panR[u] = std::sin (angle);
        }

        unisonScale = 1.0f / std::sqrt ((float) activeUnison);
        cachedDetune = p.detuneCents;
        cachedSpread = p.spread;
    }

    for (int u = 0; u < activeUnison; ++u)
        oscs[u].setMorph (p.morph);

    filter.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, p.cutoffHz));
    filter.setResonance (juce::jlimit (0.1f, 10.0f, p.resonance));

    syncRatio = juce::jlimit (0.5f, 8.0f, ratio);

    // Plain level, not the enveloped one: render() applies its own
    // per-sample follower, and taking the modulated gain here would
    // square the input envelope.
    gain = p.plainGain;

    detector.setHysteresis (hysteresis);
    envRelease = std::exp (-1.0f / (float) (juce::jmax (0.005f, envReleaseMs * 0.001f) * sampleRate));
}

void SyncVoice::render (juce::AudioBuffer<float>& output, const float* monoInput, int numSamples)
{
    if (! isPrepared || monoInput == nullptr)
        return;

    const int numChannels = output.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        const float in = monoInput[i];

        // Sync is evaluated per sample. Quantising a phase reset to a
        // block boundary would put it up to 11 ms late and destroy the
        // pitch relationship this mode exists to preserve.
        if (detector.process (in))
        {
            const float tracked = detector.getFrequency();

            if (tracked > 0.0f)
            {
                // Free-running frequency above the sync frequency is what
                // produces the classic sync timbre: the reset truncates a
                // faster cycle, and the ratio sets how bright that is.
                for (int u = 0; u < activeUnison; ++u)
                    oscs[u].setFrequency (juce::jlimit (20.0f, 20000.0f,
                                                         tracked * syncRatio * detuneRatio[u]));
            }

            for (int u = 0; u < activeUnison; ++u)
                oscs[u].sync();
        }

        const float rectified = std::abs (in);
        const float coeff = rectified > envelope ? envAttack : envRelease;
        envelope = coeff * envelope + (1.0f - coeff) * rectified;

        float left = 0.0f, right = 0.0f;

        for (int u = 0; u < activeUnison; ++u)
        {
            const float sv = oscs[u].processSample();
            left  += sv * panL[u];
            right += sv * panR[u];
        }

        left  = filter.processSample (0, left  * unisonScale);
        right = filter.processSample (1, right * unisonScale);

        const float vca = juce::jlimit (0.0f, 1.0f, envelope * 2.0f) * gain;
        left *= vca;
        right *= vca;

        if (numChannels >= 2)
        {
            output.setSample (0, i, left);
            output.setSample (1, i, right);
            for (int ch = 2; ch < numChannels; ++ch)
                output.setSample (ch, i, (left + right) * 0.5f);
        }
        else if (numChannels == 1)
        {
            output.setSample (0, i, (left + right) * 0.5f);
        }
    }
}

// ---------- SynthEngine ----------

void SynthEngine::prepare (double sampleRate, int samplesPerBlock, int numVoices)
{
    currentSampleRate = sampleRate;

    synth.clearVoices();
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice());

    // Without a registered sound, Synthesiser::noteOn matches nothing
    // and the synth is silent while compiling perfectly well.
    synth.clearSounds();
    synth.addSound (new AudioOriginSound());
    synth.addSound (new MidiOriginSound());

    synth.setCurrentPlaybackSampleRate (sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
    spec.numChannels = 1;

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            voice->prepare (spec);

    syncVoice.prepare (spec);
    syncScratch.setSize (2, juce::jmax (1, samplesPerBlock));
}

juce::AudioProcessorValueTreeState::ParameterLayout SynthEngine::createParameterLayout()
{
    using Param = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { oscModeParamID, 1 }, "Oscillator Mode",
        juce::StringArray { "Morph", "Stack" }, 0));

    // Stack slots. Off on all but the first, so switching to Stack gives
    // a plain single oscillator rather than four at once.
    for (int i = 0; i < numOscSlots; ++i)
    {
        const auto n = juce::String (i + 1);

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { oscShapeParamID (i), 1 }, "Osc " + n + " Shape",
            MorphOscillator::getShapeNames(), i == 0 ? 3 : 0));

        params.push_back (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { oscOctaveParamID (i), 1 }, "Osc " + n + " Octave",
            -2, 2, 0));

        params.push_back (std::make_unique<Param> (
            juce::ParameterID { oscDetuneParamID (i), 1 }, "Osc " + n + " Detune",
            juce::NormalisableRange<float> (-50.0f, 50.0f), 0.0f,
            ParameterFormat::floatFmt (ParameterFormat::cents)));

        params.push_back (std::make_unique<Param> (
            juce::ParameterID { oscLevelParamID (i), 1 }, "Osc " + n + " Level",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f,
            ParameterFormat::floatFmt (ParameterFormat::percent)));

        params.push_back (std::make_unique<Param> (
            juce::ParameterID { oscPanParamID (i), 1 }, "Osc " + n + " Pan",
            juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
            ParameterFormat::floatFmt (ParameterFormat::panPosition)));
    }

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { morphParamID, 1 }, "Waveshape",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f,
        ParameterFormat::floatFmt (ParameterFormat::waveshape)));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { unisonParamID, 1 }, "Unison Voices", 1, 4, 1,
        ParameterFormat::intFmt (ParameterFormat::voices)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { detuneParamID, 1 }, "Unison Detune (cents)",
        juce::NormalisableRange<float> (0.0f, 50.0f), 12.0f,
        ParameterFormat::floatFmt (ParameterFormat::cents)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { spreadParamID, 1 }, "Unison Spread",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { cutoffParamID, 1 }, "Filter Cutoff",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 1200.0f,
        ParameterFormat::floatFmt (ParameterFormat::cutoffHz)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { resonanceParamID, 1 }, "Filter Resonance",
        juce::NormalisableRange<float> (0.1f, 10.0f), 0.7f,
        ParameterFormat::floatFmt (ParameterFormat::resonanceQ)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { cutoffModDepthParamID, 1 }, "Cutoff Mod Depth (octaves)",
        juce::NormalisableRange<float> (0.0f, 6.0f), 4.0f,
        ParameterFormat::floatFmt (ParameterFormat::octaves)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { morphModDepthParamID, 1 }, "Waveshape Mod Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { attackParamID, 1 }, "Attack (ms)",
        juce::NormalisableRange<float> (1.0f, 2000.0f, 0.1f, 0.3f), 10.0f,
        ParameterFormat::floatFmt (ParameterFormat::milliseconds)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { decayParamID, 1 }, "Decay (ms)",
        juce::NormalisableRange<float> (1.0f, 2000.0f, 0.1f, 0.3f), 150.0f,
        ParameterFormat::floatFmt (ParameterFormat::milliseconds)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { sustainParamID, 1 }, "Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { releaseParamID, 1 }, "Release (ms)",
        juce::NormalisableRange<float> (1.0f, 4000.0f, 0.1f, 0.3f), 300.0f,
        ParameterFormat::floatFmt (ParameterFormat::milliseconds)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { ampLevelParamID, 1 }, "Output Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f,
        ParameterFormat::floatFmt (ParameterFormat::decibels)));

    // Narrowing this is the single largest latency reduction available:
    // the analysis window is two periods of the lowest detectable note,
    // so raising the floor shortens it proportionally.
    // A mix rather than a switch. At 0 the synth is entirely
    // note-driven; at 1 entirely hard-synced. Between the two, the sync
    // layer responds within a cycle and covers the interval during which
    // pitch detection is still deciding, so an attack is heard
    // immediately and the note layer arrives underneath it.
    params.push_back (std::make_unique<Param> (
        juce::ParameterID { syncMixParamID, 1 }, "Sync Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { audioNotesParamID, 1 }, "Audio Notes", true));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { midiNotesParamID, 1 }, "MIDI Notes", true));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { syncRatioParamID, 1 }, "Sync Ratio",
        juce::NormalisableRange<float> (0.5f, 8.0f, 0.01f, 0.5f), 1.0f,
        ParameterFormat::floatFmt (ParameterFormat::ratioAsInterval)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { syncSensParamID, 1 }, "Sync Sensitivity",
        juce::NormalisableRange<float> (0.005f, 0.30f), 0.06f,
        ParameterFormat::floatFmt (ParameterFormat::decibels)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { syncReleaseParamID, 1 }, "Sync Release (ms)",
        juce::NormalisableRange<float> (5.0f, 1000.0f, 1.0f, 0.4f), 80.0f,
        ParameterFormat::floatFmt (ParameterFormat::milliseconds)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { pitchMinParamID, 1 }, "Lowest Note (Hz)",
        juce::NormalisableRange<float> (40.0f, 500.0f, 1.0f, 0.5f), 75.0f,
        ParameterFormat::floatFmt (ParameterFormat::hzWithNote)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { pitchMaxParamID, 1 }, "Highest Note (Hz)",
        juce::NormalisableRange<float> (500.0f, 4000.0f, 1.0f, 0.5f), 2000.0f,
        ParameterFormat::floatFmt (ParameterFormat::hzWithNote)));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { adaptiveParamID, 1 }, "Adaptive", false));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { adaptRateParamID, 1 }, "Adapt Over",
        juce::NormalisableRange<float> (1.0f, 32.0f, 1.0f), 4.0f,
        ParameterFormat::floatFmt (ParameterFormat::noteCount)));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { articulationPresetParamID, 1 }, "Articulation",
        ArticulationProfile::getPresetNames(), 0));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { onsetThresholdParamID, 1 }, "Onset Threshold",
        juce::NormalisableRange<float> (0.005f, 0.30f), 0.08f,
        ParameterFormat::floatFmt (ParameterFormat::decibels)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { releaseThresholdParamID, 1 }, "Release Threshold",
        juce::NormalisableRange<float> (0.002f, 0.20f), 0.015f,
        ParameterFormat::floatFmt (ParameterFormat::decibels)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { retriggerSensParamID, 1 }, "Retrigger Threshold",
        juce::NormalisableRange<float> (0.05f, 1.0f), 0.30f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { confidenceGateParamID, 1 }, "Confidence Gate",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { pitchStabilityParamID, 1 }, "Pitch Stability (blocks)", 1, 12, 3,
        ParameterFormat::intFmt (ParameterFormat::stabilityBlocks)));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { glideModeParamID, 1 }, "Glide Mode", false));

    params.push_back (std::make_unique<Param> (
        juce::ParameterID { bendRangeParamID, 1 }, "Bend Range (semitones)",
        juce::NormalisableRange<float> (0.5f, 12.0f), 2.0f,
        ParameterFormat::floatFmt (ParameterFormat::semitones)));

    ExpressionMapper::addParameters (params);
    Arpeggiator::addParameters (params);
    EffectsChain::addParameters (params);

    return { params.begin(), params.end() };
}

VoiceParams SynthEngine::gatherParams (juce::AudioProcessorValueTreeState& params,
                                        const ModulationState& modulation) const
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
    vp.stackMode = params.getRawParameterValue (oscModeParamID)->load() > 0.5f;

    for (int i = 0; i < numOscSlots; ++i)
    {
        const int shapeIndex = (int) params.getRawParameterValue (oscShapeParamID (i))->load();

        // Index 0 is Off; the rest map onto the discrete shapes in order.
        vp.slots[i].active = shapeIndex > 0;
        vp.slots[i].shape = (MorphOscillator::Shape) shapeIndex;
        vp.slots[i].octave = (int) params.getRawParameterValue (oscOctaveParamID (i))->load();
        vp.slots[i].detuneCents = params.getRawParameterValue (oscDetuneParamID (i))->load();
        vp.slots[i].level = params.getRawParameterValue (oscLevelParamID (i))->load();
        vp.slots[i].pan = params.getRawParameterValue (oscPanParamID (i))->load();
    }

    vp.unisonCount = (int) params.getRawParameterValue (unisonParamID)->load();
    vp.detuneCents = params.getRawParameterValue (detuneParamID)->load();
    vp.spread      = params.getRawParameterValue (spreadParamID)->load();

    const float level = params.getRawParameterValue (ampLevelParamID)->load();
    vp.envelopedGain = level * modulation.amplitude.load();
    vp.plainGain = level;

    vp.adsr.attack  = params.getRawParameterValue (attackParamID)->load()  * 0.001f;
    vp.adsr.decay   = params.getRawParameterValue (decayParamID)->load()   * 0.001f;
    vp.adsr.sustain = params.getRawParameterValue (sustainParamID)->load();
    vp.adsr.release = params.getRawParameterValue (releaseParamID)->load() * 0.001f;

    return vp;
}

void SynthEngine::renderNextBlock (juce::AudioBuffer<float>& buffer,
                                    const juce::MidiBuffer& midi,
                                    juce::AudioProcessorValueTreeState& params,
                                    const ModulationState& modulation,
                                    const float* monoInput)
{
    const auto vp = gatherParams (params, modulation);
    const int numSamples = buffer.getNumSamples();

    currentSyncMix = juce::jlimit (0.0f, 1.0f,
                                    params.getRawParameterValue (syncMixParamID)->load());

    const bool useSync = currentSyncMix > 0.001f;
    const bool useNotes = currentSyncMix < 0.999f;
    const bool blending = useSync && useNotes;

    // Coming out of a blend, the sync oscillators have been free-running
    // against a signal nobody was listening to. Restart them rather than
    // fading in mid-cycle.
    if (blending != lastWasBlending && ! blending && ! useSync)
        syncVoice.reset();
    lastWasBlending = blending;

    if (useSync)
    {
        syncVoice.setParams (vp,
                              params.getRawParameterValue (syncRatioParamID)->load(),
                              params.getRawParameterValue (syncReleaseParamID)->load(),
                              params.getRawParameterValue (syncSensParamID)->load());
    }

    if (! useNotes)
    {
        syncVoice.render (buffer, monoInput, numSamples);
        return;
    }

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            voice->updateFromParams (vp);

    if (! useSync)
    {
        synth.renderNextBlock (buffer, midi, 0, numSamples);
        return;
    }

    // Both layers wanted. Sync renders to scratch, notes to the output,
    // then equal-power crossfade so the perceived level holds steady
    // across the sweep rather than dipping in the middle.
    syncScratch.setSize (juce::jmax (2, buffer.getNumChannels()), numSamples, false, false, true);
    syncScratch.clear();
    syncVoice.render (syncScratch, monoInput, numSamples);

    synth.renderNextBlock (buffer, midi, 0, numSamples);

    const float syncGain = std::sqrt (currentSyncMix);
    const float noteGain = std::sqrt (1.0f - currentSyncMix);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        buffer.applyGain (ch, 0, numSamples, noteGain);
        buffer.addFrom (ch, 0, syncScratch,
                         juce::jmin (ch, syncScratch.getNumChannels() - 1),
                         0, numSamples, syncGain);
    }
}
