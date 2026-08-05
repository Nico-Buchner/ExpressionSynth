#pragma once
#include "FeatureExtractor.h"
#include "ArticulationProfile.h"

// Measures how the source is being played and continuously repositions
// the detection parameters, rather than requiring a preset to be chosen.
//
// The five presets remain, as anchor points in descriptor space. Live
// measurements produce a position between them, and the parameters are
// interpolated accordingly. A violinist moving from legato to pizzicato
// travels between two anchors instead of needing a patch change, and the
// intermediate articulations that match no preset - spiccato, martele,
// tremolo - land at defensible intermediate positions.
//
// Two known limitations, neither avoidable:
//
//  - Adaptation necessarily lags. Descriptors are measured from notes
//    that have already been detected, so the first note of a new
//    articulation is judged by the previous one's settings. Fast
//    convergence reduces this to roughly one note; it cannot reach zero.
//
//  - The loop is closed. Changing a threshold changes which notes
//    trigger, which changes the measurement. Rate limiting and a
//    per-note step clamp keep it from hunting.
class ArticulationAnalyser
{
public:
    void prepare (double sampleRate, int blockSize);
    void reset();

    // Once per block, after FeatureExtractor::process and after
    // PitchToMidiConverter has updated its note state.
    void process (const FeatureExtractor::Features& features, int activeNote);

    const ArticulationProfile::Descriptors& getDescriptors() const noexcept { return smoothed; }
    const float* getWeights() const noexcept { return weights; }
    const ArticulationProfile& getProfile() const noexcept { return blended; }

    // Notes taken to converge on a new articulation. Lower is faster to
    // react and more prone to being pulled about by a single odd note.
    void setConvergenceNotes (float notes);

    int getObservedNoteCount() const noexcept { return notesObserved; }

private:
    void beginNote (int note, float pitchHz);
    void finishNote();
    void foldIn (const ArticulationProfile::Descriptors& measured);
    void rebuild();

    double sampleRate = 44100.0;
    float blockMs = 11.6f;
    int sustainProbeBlocks = 17;

    // Running estimate, and the blend derived from it.
    ArticulationProfile::Descriptors smoothed;
    float weights[ArticulationProfile::numPresets] { 0.2f, 0.2f, 0.2f, 0.2f, 0.2f };
    ArticulationProfile blended { ArticulationProfile::plucked() };

    float alpha = 0.25f;
    static constexpr float maxStepPerNote = 0.18f;
    int notesObserved = 0;

    // Per-note measurement state.
    bool noteActive = false;
    int currentNote = -1;
    int blocksSinceStart = 0;
    float peakAmplitude = 0.0f;
    int blocksToPeak = 0;
    float sustainAmplitude = 0.0f;
    bool sustainCaptured = false;
    float pendingGlide = 0.0f;
    bool haveGlide = false;

    float previousPitchHz = 0.0f;
    int previousNote = -1;
};
