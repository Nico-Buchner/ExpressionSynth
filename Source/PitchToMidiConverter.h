#pragma once
#include "FeatureExtractor.h"
#include "ArticulationProfile.h"
#include <juce_audio_basics/juce_audio_basics.h>

/**
    Converts continuous pitch + amplitude into discrete MIDI note events.
    Monophonic only — one note active at a time, which matches YIN's
    single-pitch-per-block assumption in FeatureExtractor.

    All behavioural thresholds live in an ArticulationProfile rather than
    being hardcoded, so they can be swapped or retuned at runtime. This
    matters practically: the build pipeline is cloud CI, so a hardcoded
    constant would cost a full rebuild cycle to adjust by ear.

    Two pitch modes (see ArticulationProfile::PitchMode):
      - Quantized: pitch changes retrigger a new note once stable.
      - Glide:     pitch changes bend the sounding note within the
                   profile's bend range; only a move beyond that range
                   retriggers.
*/
class PitchToMidiConverter
{
public:
    void prepare (double sampleRate);
    void reset();

    void setProfile (const ArticulationProfile& newProfile) { profile = newProfile; }
    const ArticulationProfile& getProfile() const noexcept { return profile; }

    // Call once per block, after FeatureExtractor::process(). Appends
    // note-on/off messages (if any state change occurred) into midiOut.
    // Resolution is block-rate — a smaller block size lowers trigger
    // latency at the cost of more frequent YIN/FFT computation.
    void process (const FeatureExtractor::Features& features,
                  juce::MidiBuffer& midiOut, int numSamples);

    // Currently sounding note, or -1 if none. Used by PluginProcessor to
    // compute cents deviation (fine pitch expression) against the
    // discrete note this class has already selected.
    int getCurrentNote() const noexcept { return currentNote; }

    static float midiNoteToFrequency (int note);

private:
    static int frequencyToMidiNote (float hz);
    static int amplitudeToVelocity (float amplitude);

    // Emits noteOff+noteOn for a new note and resets retrigger state.
    void triggerNote (int note, float amplitude, juce::MidiBuffer& midiOut, bool releaseFirst);

    ArticulationProfile profile { ArticulationProfile::plucked() };

    bool noteIsOn = false;
    int currentNote = -1;

    int candidateNote = -1;
    int candidateHoldCount = 0;

    int blocksSinceLastTrigger = 0;

    double sampleRate = 44100.0;
};
