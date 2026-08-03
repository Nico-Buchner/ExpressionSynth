#pragma once
#include <atomic>

/**
    Per-block modulation values produced by analysis and consumed by the
    synth.

    This exists to keep modulation OFF the host parameter system.
    ExpressionMapper originally drove synth parameters by calling
    setValueNotifyingHost() every block from the audio thread, which is
    wrong in three ways: it tells the host a user moved a control
    (spamming automation lanes in Logic), it is not guaranteed
    real-time-safe, and it conflates "the value the user dialled in"
    with "the value analysis is currently modulating it to".

    So: APVTS parameters hold the user's *base* settings and remain
    normally automatable, while these hold the live modulation applied on
    top. The synth combines the two at render time.

    Atomics are used so the editor can read current values for display
    without a data race; production and consumption both happen on the
    audio thread.
*/
struct ModulationState
{
    // 0-1 modulation amounts applied on top of the user's base values.
    std::atomic<float> filterCutoff    { 0.0f };
    std::atomic<float> filterResonance { 0.0f };

    // Direct gain multiplier, 0-1.
    std::atomic<float> amplitude       { 1.0f };

    // Absolute, in semitones — carries cents-deviation pitch expression
    // (vibrato/bend) on top of the note PitchToMidiConverter selected.
    std::atomic<float> pitchBendSemitones { 0.0f };

    void reset()
    {
        filterCutoff.store (0.0f);
        filterResonance.store (0.0f);
        amplitude.store (1.0f);
        pitchBendSemitones.store (0.0f);
    }
};
