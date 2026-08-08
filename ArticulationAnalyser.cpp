#include "ArticulationAnalyser.h"

void ArticulationAnalyser::prepare (double newSampleRate, int blockSize)
{
    sampleRate = newSampleRate;
    blockMs = 1000.0f * (float) juce::jmax (1, blockSize) / (float) sampleRate;

    // Sustain is probed a little after the attack has settled, far enough
    // in that a decaying source has visibly decayed and a sustained one
    // has not.
    sustainProbeBlocks = juce::jmax (2, (int) std::round (200.0f / blockMs));

    reset();
}

void ArticulationAnalyser::reset()
{
    smoothed = ArticulationProfile::positionOf (0);
    notesObserved = 0;
    noteActive = false;
    currentNote = -1;
    previousNote = -1;
    previousPitchHz = 0.0f;
    haveGlide = false;
    pendingGlide = 0.0f;
    rebuild();
}

void ArticulationAnalyser::setConvergenceNotes (float notes)
{
    notes = juce::jlimit (1.0f, 32.0f, notes);
    alpha = 1.0f - std::exp (-1.0f / notes);
}

void ArticulationAnalyser::process (const FeatureExtractor::Features& features, int activeNote)
{
    // --- note boundaries -------------------------------------------------
    if (activeNote != currentNote)
    {
        // A pitch change with the note still sounding is a transition, and
        // the pitch just before it tells us whether the player travelled
        // between the notes or jumped. That is the glide measurement.
        if (activeNote >= 0 && currentNote >= 0 && previousPitchHz > 0.0f)
        {
            const float fromHz = 440.0f * std::pow (2.0f, ((float) currentNote - 69.0f) / 12.0f);
            const float toHz   = 440.0f * std::pow (2.0f, ((float) activeNote - 69.0f) / 12.0f);
            const float span   = std::abs (toHz - fromHz);

            if (span > 0.1f)
            {
                const float travelled = std::abs (previousPitchHz - fromHz) / span;
                pendingGlide = juce::jlimit (0.0f, 1.0f, travelled);
                haveGlide = true;
            }
        }

        if (noteActive)
            finishNote();

        if (activeNote >= 0)
            beginNote (activeNote, features.pitchHz);

        currentNote = activeNote;
    }

    // --- accumulate within the note --------------------------------------
    if (noteActive)
    {
        ++blocksSinceStart;

        if (features.amplitude > peakAmplitude)
        {
            peakAmplitude = features.amplitude;
            blocksToPeak = blocksSinceStart;
        }

        if (! sustainCaptured && blocksSinceStart >= sustainProbeBlocks)
        {
            sustainAmplitude = features.amplitude;
            sustainCaptured = true;
        }
    }

    if (features.pitchHz > 0.0f)
        previousPitchHz = features.pitchHz;
}

void ArticulationAnalyser::beginNote (int note, float pitchHz)
{
    juce::ignoreUnused (pitchHz);
    noteActive = true;
    blocksSinceStart = 0;
    peakAmplitude = 0.0f;
    blocksToPeak = 0;
    sustainAmplitude = 0.0f;
    sustainCaptured = false;
    previousNote = note;
}

void ArticulationAnalyser::finishNote()
{
    noteActive = false;

    // Too short to have measured anything meaningful.
    if (peakAmplitude < 1.0e-4f || blocksSinceStart < 2)
        return;

    ArticulationProfile::Descriptors measured = smoothed;

    // Sustain: energy remaining some way past the attack, relative to the
    // peak. A plucked string has largely gone; a bowed one has not.
    if (sustainCaptured)
        measured.sustain = juce::jlimit (0.0f, 1.0f, sustainAmplitude / peakAmplitude);
    else
        measured.sustain = 0.0f;   // released before the probe: impulsive

    // Sharpness: rise time to peak, mapped so a few milliseconds reads as
    // near 1 and a hundred milliseconds as near 0.
    const float riseMs = juce::jmax (1.0f, (float) blocksToPeak * blockMs);
    measured.sharpness = juce::jlimit (0.0f, 1.0f, std::exp (-riseMs / 30.0f));

    if (haveGlide)
    {
        measured.glide = pendingGlide;
        haveGlide = false;
    }

    foldIn (measured);
}

void ArticulationAnalyser::foldIn (const ArticulationProfile::Descriptors& measured)
{
    // Exponential average, with a hard clamp on movement per note so one
    // unusual note cannot swing the whole estimate.
    auto step = [this] (float current, float target)
    {
        const float delta = juce::jlimit (-maxStepPerNote, maxStepPerNote,
                                           (target - current) * alpha);
        return juce::jlimit (0.0f, 1.0f, current + delta);
    };

    smoothed.sustain   = step (smoothed.sustain,   measured.sustain);
    smoothed.sharpness = step (smoothed.sharpness, measured.sharpness);
    smoothed.glide     = step (smoothed.glide,     measured.glide);

    ++notesObserved;
    rebuild();
}

void ArticulationAnalyser::rebuild()
{
    ArticulationProfile::weightsFor (smoothed, weights);
    blended = ArticulationProfile::blended (weights);
}
