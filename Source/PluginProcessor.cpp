#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NoteOrigin.h"

ExpressionSynthProcessor::ExpressionSynthProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", SynthEngine::createParameterLayout())
{

    // Listen for articulation changes so the converter's profile stays
    // in sync with the parameters without polling every block.
    for (auto* id : { SynthEngine::articulationPresetParamID,
                       SynthEngine::onsetThresholdParamID,
                       SynthEngine::releaseThresholdParamID,
                       SynthEngine::retriggerSensParamID,
                       SynthEngine::confidenceGateParamID,
                       SynthEngine::pitchStabilityParamID,
                       SynthEngine::glideModeParamID,
                       SynthEngine::bendRangeParamID,
                       SynthEngine::adaptiveParamID,
                       SynthEngine::adaptRateParamID,
                       SynthEngine::pitchMinParamID,
                       SynthEngine::pitchMaxParamID,
                       SynthEngine::syncMixParamID })
        apvts.addParameterListener (id, this);

    refreshArticulationProfile();
}

ExpressionSynthProcessor::~ExpressionSynthProcessor()
{
    for (auto* id : { SynthEngine::articulationPresetParamID,
                       SynthEngine::onsetThresholdParamID,
                       SynthEngine::releaseThresholdParamID,
                       SynthEngine::retriggerSensParamID,
                       SynthEngine::confidenceGateParamID,
                       SynthEngine::pitchStabilityParamID,
                       SynthEngine::glideModeParamID,
                       SynthEngine::bendRangeParamID,
                       SynthEngine::adaptiveParamID,
                       SynthEngine::adaptRateParamID,
                       SynthEngine::pitchMinParamID,
                       SynthEngine::pitchMaxParamID,
                       SynthEngine::syncMixParamID })
        apvts.removeParameterListener (id, this);

    cancelPendingUpdate();
}

void ExpressionSynthProcessor::parameterChanged (const juce::String& paramID, float)
{
    if (paramID == SynthEngine::articulationPresetParamID)
    {
        // Selecting a preset overwrites the individual params with that
        // preset's values - the user can then tweak from there, the way
        // a hardware synth preset behaves. Deferred to the message
        // thread because writing host-visible parameters from the audio
        // thread is not real-time safe.
        triggerAsyncUpdate();
    }

    refreshArticulationProfile();
}

void ExpressionSynthProcessor::refreshArticulationProfile()
{
    featureExtractor.setPitchRange (
        apvts.getRawParameterValue (SynthEngine::pitchMinParamID)->load(),
        apvts.getRawParameterValue (SynthEngine::pitchMaxParamID)->load());

    adaptiveActive.store (apvts.getRawParameterValue (SynthEngine::adaptiveParamID)->load() > 0.5f);
    articulationAnalyser.setConvergenceNotes (
        apvts.getRawParameterValue (SynthEngine::adaptRateParamID)->load());

    // In adaptive mode the analyser owns the profile outright; the manual
    // parameters stay where the user left them so switching back is
    // lossless, and so the two can be compared on the same material.
    if (adaptiveActive.load())
    {
        const auto adapted = articulationAnalyser.getProfile();
        pitchToMidi.setProfile (adapted);
        featureExtractor.setEnvelopeTimes (adapted.ampAttackMs, adapted.ampReleaseMs);
        return;
    }

    const int presetIndex = (int) apvts.getRawParameterValue (SynthEngine::articulationPresetParamID)->load();

    // Start from the preset (which carries envelope times and debounce
    // values not individually exposed), then override with the live
    // params the user can adjust.
    auto profile = ArticulationProfile::fromPresetIndex (presetIndex);

    profile.onsetAmplitudeThreshold   = apvts.getRawParameterValue (SynthEngine::onsetThresholdParamID)->load();
    profile.releaseAmplitudeThreshold = apvts.getRawParameterValue (SynthEngine::releaseThresholdParamID)->load();
    profile.onsetRetriggerThreshold   = apvts.getRawParameterValue (SynthEngine::retriggerSensParamID)->load();
    profile.minPitchConfidence        = apvts.getRawParameterValue (SynthEngine::confidenceGateParamID)->load();
    profile.stableBlocksRequiredForRetrigger = (int) apvts.getRawParameterValue (SynthEngine::pitchStabilityParamID)->load();
    profile.bendRangeSemitones        = apvts.getRawParameterValue (SynthEngine::bendRangeParamID)->load();
    profile.pitchMode = apvts.getRawParameterValue (SynthEngine::glideModeParamID)->load() > 0.5f
                          ? ArticulationProfile::PitchMode::Glide
                          : ArticulationProfile::PitchMode::Quantized;

    pitchToMidi.setProfile (profile);
    featureExtractor.setEnvelopeTimes (profile.ampAttackMs, profile.ampReleaseMs);
}

void ExpressionSynthProcessor::handleAsyncUpdate()
{
    // Runs on the message thread. Copies the selected preset's values
    // into the individual parameters so the UI reflects them and the
    // user can tweak onward from that starting point.
    const int presetIndex = (int) apvts.getRawParameterValue (SynthEngine::articulationPresetParamID)->load();
    const auto preset = ArticulationProfile::fromPresetIndex (presetIndex);

    auto setParam = [this] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    };

    setParam (SynthEngine::onsetThresholdParamID,   preset.onsetAmplitudeThreshold);
    setParam (SynthEngine::releaseThresholdParamID, preset.releaseAmplitudeThreshold);
    setParam (SynthEngine::retriggerSensParamID,    preset.onsetRetriggerThreshold);
    setParam (SynthEngine::pitchStabilityParamID,   (float) preset.stableBlocksRequiredForRetrigger);
    setParam (SynthEngine::bendRangeParamID,        preset.bendRangeSemitones);
    setParam (SynthEngine::glideModeParamID,
               preset.pitchMode == ArticulationProfile::PitchMode::Glide ? 1.0f : 0.0f);
}

void ExpressionSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    featureExtractor.prepare (sampleRate, samplesPerBlock);
    expressionMapper.prepare (sampleRate);
    pitchToMidi.prepare (sampleRate);
    articulationAnalyser.prepare (sampleRate, samplesPerBlock);
    arpeggiator.prepare (sampleRate);
    effects.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    synthEngine.prepare (sampleRate, samplesPerBlock);
    analysisBuffer.setSize (1, samplesPerBlock);

    pitchBendSmoother.reset (sampleRate, 0.03); // 30ms - fast enough for vibrato, no zipper noise
    pitchBendSmoother.setCurrentAndTargetValue (0.0f);
    modulation.reset();

    // prepare() resets envelope coefficients to defaults, so re-apply
    // the active profile's attack/release afterwards.
    refreshArticulationProfile();
}

void ExpressionSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{

    // 1. Sum input to mono for analysis (don't touch `buffer` yet - we
    //    need it clean for the synth to render into afterward).
    analysisBuffer.setSize (1, buffer.getNumSamples(), false, false, true);
    analysisBuffer.clear();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        analysisBuffer.addFrom (0, 0, buffer, ch, 0, buffer.getNumSamples(),
                                 1.0f / (float) buffer.getNumChannels());

    // 2. Extract expression features from the input audio.
    featureExtractor.process (analysisBuffer);
    const auto& features = featureExtractor.getLatestFeatures();

    // 3. Convert pitch/amplitude into note-on/off events, then merge any
    //    notes the player is holding on a keyboard. Both end up in the
    //    same voice pool; they are told apart by channel, so the
    //    modulations that belong to a detected note are not applied to a
    //    note somebody fingered.
    rawMidi.clear();
    generatedMidi.clear();

    if (apvts.getRawParameterValue (SynthEngine::audioNotesParamID)->load() > 0.5f)
    {
        pitchToMidi.process (features, rawMidi, buffer.getNumSamples());
    }
    else if (pitchToMidi.getCurrentNote() >= 0)
    {
        // Audio triggering was switched off mid-note; release it rather
        // than leaving it sounding forever.
        pitchToMidi.reset();
        rawMidi.addEvent (juce::MidiMessage::allNotesOff (NoteOrigin::audioChannel), 0);
    }

    if (apvts.getRawParameterValue (SynthEngine::midiNotesParamID)->load() > 0.5f)
        rawMidi.addEvents (midi, 0, buffer.getNumSamples(), 0);

    // 3c. The arpeggiator replaces those notes with a pattern built from
    //     them, or passes them straight through when it is off. Tempo
    //     comes from the host where there is one, otherwise from its own
    //     free-running setting.
    double bpm = apvts.getRawParameterValue (Arpeggiator::freeBpmParamID)->load();

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;

    arpeggiator.setParams (apvts);
    arpeggiator.process (rawMidi, pitchToMidi.getCurrentNote(),
                          generatedMidi, buffer.getNumSamples(), bpm);

    // 3b. Watch how the source is being played. In adaptive mode this
    //     feeds straight back into the detection thresholds, so a player
    //     changing articulation mid-phrase is followed rather than
    //     requiring a patch change. The loop is closed deliberately;
    //     the analyser rate-limits itself to keep it from hunting.
    articulationAnalyser.process (features, pitchToMidi.getCurrentNote());

    if (adaptiveActive.load())
    {
        const auto& adapted = articulationAnalyser.getProfile();
        pitchToMidi.setProfile (adapted);
        featureExtractor.setEnvelopeTimes (adapted.ampAttackMs, adapted.ampReleaseMs);
    }

    // 4. Fine pitch expression first: the cents deviation of the detected
    //    pitch from the note it was quantised to. The matrix runs after
    //    this so that a slot routed to bend adds to it rather than
    //    overwriting a note's tuning.
    const int activeNote = pitchToMidi.getCurrentNote();
    if (activeNote >= 0 && features.pitchHz > 0.0f)
    {
        const float noteHz = PitchToMidiConverter::midiNoteToFrequency (activeNote);
        const float semitonesAway = 12.0f * std::log2 (features.pitchHz / noteHz);
        const float bendRange = pitchToMidi.getProfile().bendRangeSemitones;
        pitchBendSmoother.setTargetValue (juce::jlimit (-bendRange, bendRange, semitonesAway));
    }
    else
    {
        pitchBendSmoother.setTargetValue (0.0f);
    }

    pitchBendSmoother.skip (juce::jmax (0, buffer.getNumSamples() - 1));
    modulation.pitchBendSemitones.store (pitchBendSmoother.getNextValue());

    // 5. Apply the routing matrix.
    expressionMapper.apply (features, modulation, buffer.getNumSamples(), apvts);

    // 6. Render. The input is replaced, not passed through - but sync
    //    mode reads the mono sum, so the analysis buffer is handed over
    //    rather than the output buffer being cleared first.
    const float* monoInput = analysisBuffer.getReadPointer (0);
    buffer.clear();
    synthEngine.renderNextBlock (buffer, generatedMidi, apvts, modulation, monoInput);

    // 7. Effects run once on the mix, so their cost is independent of how
    //    many voices are sounding. They share the same tempo the
    //    arpeggiator uses.
    effects.process (buffer, apvts, modulation, bpm);
}

juce::AudioProcessorEditor* ExpressionSynthProcessor::createEditor()
{
    return new ExpressionSynthEditor (*this);
}

void ExpressionSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState().createXml())
        copyXmlToBinary (*state, destData);
}

void ExpressionSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ExpressionSynthProcessor();
}
