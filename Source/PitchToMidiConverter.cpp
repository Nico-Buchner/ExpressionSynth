#include "PitchToMidiConverter.h"

void PitchToMidiConverter::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    reset();
}

void PitchToMidiConverter::reset()
{
    noteIsOn = false;
    currentNote = -1;
    candidateNote = -1;
    candidateHoldCount = 0;
    blocksSinceLastTrigger = 0;
}

int PitchToMidiConverter::frequencyToMidiNote (float hz)
{
    // Standard equal-temperament conversion, A4 = 440Hz = MIDI note 69.
    return (int) std::round (69.0f + 12.0f * std::log2 (hz / 440.0f));
}

int PitchToMidiConverter::amplitudeToVelocity (float amplitude)
{
    return juce::jlimit (1, 127, (int) std::round (amplitude * 127.0f));
}

float PitchToMidiConverter::midiNoteToFrequency (int note)
{
    return 440.0f * std::pow (2.0f, ((float) note - 69.0f) / 12.0f);
}

void PitchToMidiConverter::triggerNote (int note, float amplitude,
                                         juce::MidiBuffer& midiOut, bool releaseFirst)
{
    if (releaseFirst && currentNote >= 0)
        midiOut.addEvent (juce::MidiMessage::noteOff (1, currentNote), 0);

    const int velocity = amplitudeToVelocity (amplitude);
    midiOut.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity),
                       releaseFirst ? 1 : 0);

    currentNote = note;
    noteIsOn = true;
    candidateNote = -1;
    candidateHoldCount = 0;
    blocksSinceLastTrigger = 0;
}

void PitchToMidiConverter::process (const FeatureExtractor::Features& features,
                                     juce::MidiBuffer& midiOut, int numSamples)
{
    const bool pitchIsUsable = features.pitchConfidence >= profile.minPitchConfidence
                                 && features.pitchHz > 0.0f;
    const int samplePos = juce::jmax (0, numSamples - 1); // place event near block end

    if (! noteIsOn)
    {
        // --- Looking for a note-on trigger ---
        if (pitchIsUsable && features.amplitude >= profile.onsetAmplitudeThreshold)
            triggerNote (frequencyToMidiNote (features.pitchHz), features.amplitude,
                          midiOut, false);
        return;
    }

    // --- A note is currently on: watch for release or retrigger ---
    if (! pitchIsUsable || features.amplitude < profile.releaseAmplitudeThreshold)
    {
        midiOut.addEvent (juce::MidiMessage::noteOff (1, currentNote), samplePos);
        noteIsOn = false;
        currentNote = -1;
        candidateNote = -1;
        candidateHoldCount = 0;
        blocksSinceLastTrigger = 0;
        return;
    }

    ++blocksSinceLastTrigger;

    const int detectedNote = frequencyToMidiNote (features.pitchHz);

    if (detectedNote != currentNote)
    {
        // In Glide mode, a pitch move within the bend range is expression,
        // not a new note - PluginProcessor's cents-deviation calculation
        // bends the sounding note instead. Only a move beyond what the
        // bend range can represent forces a retrigger.
        if (profile.pitchMode == ArticulationProfile::PitchMode::Glide)
        {
            const float noteHz = midiNoteToFrequency (currentNote);
            const float semitonesAway = 12.0f * std::log2 (features.pitchHz / noteHz);

            if (std::abs (semitonesAway) <= profile.bendRangeSemitones)
            {
                candidateNote = -1;
                candidateHoldCount = 0;
                return; // stay on the current note and let it bend
            }
            // else: fall through to the stability check and retrigger
        }

        // Require the new pitch to persist for several consecutive blocks
        // before retriggering, so one noisy YIN estimate mid-note doesn't
        // cause a spurious note change.
        if (detectedNote == candidateNote)
            ++candidateHoldCount;
        else
        {
            candidateNote = detectedNote;
            candidateHoldCount = 1;
        }

        if (candidateHoldCount >= profile.stableBlocksRequiredForRetrigger)
            triggerNote (detectedNote, features.amplitude, midiOut, true);

        return;
    }

    // Pitch unchanged - clear any pending candidate from a prior block.
    candidateNote = -1;
    candidateHoldCount = 0;

    // Same-pitch re-attack: a fresh onset (re-picked string, re-tongued
    // note, restruck key) while still sustaining the same note. Gated
    // by a debounce window so the attack transient of the *current*
    // note - which itself spiked onsetStrength when it started - can't
    // trigger a second, spurious retrigger a block or two later.
    if (features.onsetStrength >= profile.onsetRetriggerThreshold
        && blocksSinceLastTrigger >= profile.retriggerDebounceBlocks)
    {
        triggerNote (currentNote, features.amplitude, midiOut, true);
    }
}
