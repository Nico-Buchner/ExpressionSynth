#pragma once

// Notes reach the voice pool from two places: detected from the incoming
// audio, or played on a keyboard. Some modulations describe the detected
// note and must not be applied to a note the player fingered themselves,
// so a voice needs to know which it is holding.
//
// JUCE's Synthesiser offers no direct channel for that, but it does
// dispatch by MIDI channel. Audio-generated notes are therefore emitted
// on a reserved channel, and two SynthesiserSound types separate on it.
namespace NoteOrigin
{
    static constexpr int audioChannel = 16;
}
