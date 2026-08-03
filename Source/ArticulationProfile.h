#pragma once
#include <juce_core/juce_core.h>

/**
    A named set of note-detection thresholds tuned for one class of
    instrument articulation. Different sources behave very differently
    at the analysis level:

      - a plucked string has a sharp attack and immediate decay
      - a bowed string can start almost silently and swell
      - a tongued wind note re-attacks at the same pitch constantly
      - a voice slides between pitches and produces false onsets on
        consonants

    A single threshold set can't serve all of these, so the converter
    reads its behaviour from one of these instead of hardcoded values.

    All fields are plain data — PitchToMidiConverter holds one by value
    and PluginProcessor swaps it at runtime from APVTS parameters, so
    retuning never requires a rebuild.
*/
struct ArticulationProfile
{
    enum class PitchMode
    {
        // Pitch changes retrigger a new note once stable. Correct for
        // fretted/keyed instruments where pitch moves in discrete steps.
        Quantized,

        // Pitch changes bend the sounding note instead of retriggering,
        // up to bendRangeSemitones — beyond that a retrigger still
        // fires. Correct for voice, fretless strings, slide guitar,
        // anything with meaningful portamento.
        Glide
    };

    juce::String name { "Custom" };

    // --- Note on/off ---
    // Onset threshold is deliberately higher than release (hysteresis)
    // so amplitude sitting near one value can't flutter the note.
    float onsetAmplitudeThreshold   = 0.06f;
    float releaseAmplitudeThreshold = 0.02f;
    float minPitchConfidence        = 0.5f;

    // --- Retrigger behaviour ---
    int   stableBlocksRequiredForRetrigger = 3;
    float onsetRetriggerThreshold          = 0.35f;
    int   retriggerDebounceBlocks          = 4;

    // --- Pitch handling ---
    PitchMode pitchMode         = PitchMode::Quantized;
    float     bendRangeSemitones = 2.0f;

    // --- Amplitude envelope response (applied to FeatureExtractor) ---
    // Attack governs how fast a note is detected; release governs how
    // long it hangs on through decay before note-off.
    float ampAttackMs  = 5.0f;
    float ampReleaseMs = 80.0f;

    // ---- Presets ----
    // Starting points tuned by reasoning about each source's envelope
    // and pitch behaviour, NOT by measurement — expect to adjust these
    // by ear on first test. That's exactly why they're runtime-editable.

    /** Guitar, harp, pizzicato strings, clav. Sharp attack, immediate
        decay, stable pitch once the attack transient passes. */
    static ArticulationProfile plucked()
    {
        ArticulationProfile p;
        p.name = "Plucked";
        p.onsetAmplitudeThreshold = 0.08f;   // clear attack, can afford a high bar
        p.releaseAmplitudeThreshold = 0.015f; // hang on through the decay tail
        p.stableBlocksRequiredForRetrigger = 3;
        p.onsetRetriggerThreshold = 0.30f;
        p.retriggerDebounceBlocks = 3;
        p.pitchMode = PitchMode::Quantized;
        p.bendRangeSemitones = 2.0f;
        p.ampAttackMs = 3.0f;                 // fast, to catch the pluck
        p.ampReleaseMs = 120.0f;              // slow, to ride the decay
        return p;
    }

    /** Violin, cello, viola. Can start near-silent and swell; bow
        changes must NOT retrigger; vibrato must not be read as a
        pitch change. */
    static ArticulationProfile bowed()
    {
        ArticulationProfile p;
        p.name = "Bowed";
        p.onsetAmplitudeThreshold = 0.035f;  // low — a bow can enter very quietly
        p.releaseAmplitudeThreshold = 0.015f;
        p.stableBlocksRequiredForRetrigger = 5; // vibrato-tolerant
        p.onsetRetriggerThreshold = 0.55f;   // high — bow changes shouldn't retrigger
        p.retriggerDebounceBlocks = 8;
        p.pitchMode = PitchMode::Glide;      // portamento is idiomatic
        p.bendRangeSemitones = 2.0f;
        p.ampAttackMs = 15.0f;               // slower, matches gradual bow onset
        p.ampReleaseMs = 100.0f;
        return p;
    }

    /** Flute, sax, clarinet, trumpet. Breath onset can be gradual, but
        tonguing produces frequent sharp same-pitch re-attacks that
        SHOULD retrigger. */
    static ArticulationProfile wind()
    {
        ArticulationProfile p;
        p.name = "Wind";
        p.onsetAmplitudeThreshold = 0.05f;
        p.releaseAmplitudeThreshold = 0.02f;
        p.stableBlocksRequiredForRetrigger = 3;
        p.onsetRetriggerThreshold = 0.25f;   // sensitive — tonguing is the point
        p.retriggerDebounceBlocks = 3;       // short, to allow fast tonguing
        p.pitchMode = PitchMode::Quantized;
        p.bendRangeSemitones = 2.0f;
        p.ampAttackMs = 8.0f;
        p.ampReleaseMs = 70.0f;
        return p;
    }

    /** Singing voice. Pitch slides between notes are musical, not
        errors; consonants ("t", "k", "p") produce sharp amplitude
        spikes that are NOT new notes. */
    static ArticulationProfile vocal()
    {
        ArticulationProfile p;
        p.name = "Vocal";
        p.onsetAmplitudeThreshold = 0.05f;
        p.releaseAmplitudeThreshold = 0.018f;
        p.stableBlocksRequiredForRetrigger = 6; // very tolerant of scoops/slides
        p.onsetRetriggerThreshold = 0.60f;   // high — reject consonant transients
        p.retriggerDebounceBlocks = 10;
        p.pitchMode = PitchMode::Glide;      // scoops and slides preserved as bend
        p.bendRangeSemitones = 3.0f;         // wider, voices slide further
        p.ampAttackMs = 10.0f;
        p.ampReleaseMs = 90.0f;
        return p;
    }

    /** Marimba, kalimba, piano, mallet percussion. Very sharp attack,
        fast decay, rapid repeated notes at the same pitch. */
    static ArticulationProfile percussive()
    {
        ArticulationProfile p;
        p.name = "Percussive";
        p.onsetAmplitudeThreshold = 0.10f;   // high — attacks are unambiguous
        p.releaseAmplitudeThreshold = 0.02f;
        p.stableBlocksRequiredForRetrigger = 2;
        p.onsetRetriggerThreshold = 0.22f;   // sensitive, for fast repeats
        p.retriggerDebounceBlocks = 2;       // very short debounce
        p.pitchMode = PitchMode::Quantized;
        p.bendRangeSemitones = 1.0f;
        p.ampAttackMs = 2.0f;                // fastest — catch the strike
        p.ampReleaseMs = 60.0f;
        return p;
    }

    static juce::StringArray getPresetNames()
    {
        return { "Plucked", "Bowed", "Wind", "Vocal", "Percussive" };
    }

    static ArticulationProfile fromPresetIndex (int index)
    {
        switch (index)
        {
            case 0:  return plucked();
            case 1:  return bowed();
            case 2:  return wind();
            case 3:  return vocal();
            case 4:  return percussive();
            default: return plucked();
        }
    }
};
