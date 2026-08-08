#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "NoteOrigin.h"

// Sits between note generation and the synth, replacing whatever notes
// arrive with a rhythmic pattern built from them.
//
// The unusual part is where the notes come from. A conventional
// arpeggiator holds a chord, but audio detection is monophonic and so
// supplies no chord to hold. Three sources are offered:
//
//   MidiHeld      conventional - whatever is held on a keyboard
//   Generated     a chord built upward from the detected note
//   LatchedAudio  notes played into the plugin accumulate into a pool
//                 and are replayed as a pattern
//
// The third keeps the plugin's premise intact: the instrument still
// supplies every note, they are simply replayed rhythmically rather than
// one at a time.
//
// Arp notes keep the MIDI channel of whatever fed them, so an
// audio-derived pattern still carries the live input's envelope while a
// keyboard-derived one plays at its own level. That falls out of the
// origin split already in place and needs nothing new.
class Arpeggiator
{
public:
    static constexpr int maxPool = 8;
    static constexpr int maxOctaves = 4;

    enum class Source { MidiHeld, Generated, LatchedAudio };
    enum class Pattern { Up, Down, UpDown, DownUp, AsPlayed, Random };

    static juce::StringArray getSourceNames()
    {
        return { "MIDI held", "Generated", "Latched audio" };
    }

    static juce::StringArray getPatternNames()
    {
        return { "Up", "Down", "Up-down", "Down-up", "As played", "Random" };
    }

    static juce::StringArray getRateNames()
    {
        return { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    }

    static juce::StringArray getChordNames()
    {
        return { "Major", "Minor", "Major 7", "Minor 7", "Sus 4", "Fifth", "Octave" };
    }

    static constexpr auto enabledParamID = "arpOn";
    static constexpr auto sourceParamID  = "arpSource";
    static constexpr auto patternParamID = "arpPattern";
    static constexpr auto rateParamID    = "arpRate";
    static constexpr auto octavesParamID = "arpOctaves";
    static constexpr auto gateParamID    = "arpGate";
    static constexpr auto swingParamID   = "arpSwing";
    static constexpr auto chordParamID   = "arpChord";
    static constexpr auto freeBpmParamID = "arpFreeBpm";

    static void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params);

    void prepare (double sampleRate);
    void reset();

    void setParams (juce::AudioProcessorValueTreeState& params);

    // Consumes the notes that would have played and emits the pattern.
    void process (const juce::MidiBuffer& input, int detectedNote,
                   juce::MidiBuffer& output, int numSamples, double bpm);

    bool isEnabled() const noexcept { return enabled; }

private:
    struct PoolNote { int note; int channel; };
    struct PendingOff { int note; int channel; int samplesRemaining; };

    void collectNotes (const juce::MidiBuffer& input, int detectedNote);
    void rebuildGenerated (int rootNote);
    void addToPool (int note, int channel, bool allowDuplicates);
    int  indexForStep (int step, int poolSize) const;
    double stepLengthSamples (double bpm) const;

    std::vector<PoolNote> pool;
    std::vector<PendingOff> pendingOffs;

    bool enabled = false;
    Source source = Source::MidiHeld;
    Pattern pattern = Pattern::Up;
    int rateIndex = 1;
    int octaves = 1;
    float gate = 0.5f;
    float swing = 0.0f;
    int chordIndex = 0;
    float freeBpm = 120.0f;

    int stepCounter = 0;
    double samplesToNextStep = 0.0;
    int lastDetectedNote = -1;
    int silenceSamples = 0;

    // A latched pool that never emptied would trap the first thing played
    // for the rest of the session. Stopping for a couple of seconds
    // clears it, which is the gesture a player would expect.
    int latchClearSamples = 88200;

    juce::Random random;
    double sampleRate = 44100.0;
};
