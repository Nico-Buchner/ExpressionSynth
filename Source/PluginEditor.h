#pragma once
#include "PluginProcessor.h"

/**
    Live diagnostic readout of what the analyser is actually detecting.

    This exists because of a specific practical constraint: builds go
    through cloud CI, so a silent failure ("I play a note and nothing
    happens") would otherwise cost a full rebuild cycle just to find out
    whether the problem is pitch confidence, an amplitude threshold, or
    the synth itself. Showing the raw feature values on-device turns that
    into a glance.

    Read these while testing each articulation:
      - Amp below the onset threshold  -> nothing will ever trigger
      - Confidence low                 -> YIN isn't locking on; check input
                                          level, or the source may be too
                                          noisy/polyphonic
      - Note stuck on                  -> release threshold too low
      - Note flickering                -> raise pitch stability, or lower
                                          retrigger sensitivity
*/
class ExpressionSynthEditor : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit ExpressionSynthEditor (ExpressionSynthProcessor& p)
        : AudioProcessorEditor (&p), processor (p)
    {
        setSize (460, 340);
        startTimerHz (24); // fast enough to see transients, cheap enough for iPad
    }

    ~ExpressionSynthEditor() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1e1e24));

        const auto features = processor.getFeaturesForDisplay();
        const int activeNote = processor.getActiveNoteForDisplay();

        auto bounds = getLocalBounds().reduced (16);

        // --- Header ---
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (18.0f).withStyle ("Bold"));
        g.drawText ("ExpressionSynth", bounds.removeFromTop (26),
                     juce::Justification::centredLeft);

        g.setColour (juce::Colour (0xff9a9aa8));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Articulation: " + processor.getActiveProfileName(),
                     bounds.removeFromTop (20), juce::Justification::centredLeft);

        bounds.removeFromTop (10);

        // --- Note state, the single most useful line while testing ---
        g.setFont (juce::FontOptions (15.0f).withStyle ("Bold"));
        if (activeNote >= 0)
        {
            g.setColour (juce::Colour (0xff6ee7a0));
            g.drawText ("NOTE ON   " + midiNoteName (activeNote)
                          + "   (" + juce::String (activeNote) + ")",
                         bounds.removeFromTop (24), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (juce::Colour (0xff6a6a78));
            g.drawText ("- silent -", bounds.removeFromTop (24),
                         juce::Justification::centredLeft);
        }

        bounds.removeFromTop (8);

        // --- Feature meters ---
        drawMeter (g, bounds.removeFromTop (30), "Amplitude", features.amplitude,
                    juce::Colour (0xff5aa9e6));
        drawMeter (g, bounds.removeFromTop (30), "Confidence", features.pitchConfidence,
                    features.pitchConfidence >= 0.5f ? juce::Colour (0xff6ee7a0)
                                                      : juce::Colour (0xffe6a15a));
        drawMeter (g, bounds.removeFromTop (30), "Onset", features.onsetStrength,
                    juce::Colour (0xffe65a8b));
        drawMeter (g, bounds.removeFromTop (30), "Brightness", features.spectralCentroid,
                    juce::Colour (0xffb98ae6));
        drawMeter (g, bounds.removeFromTop (30), "Noisiness", features.spectralFlatness,
                    juce::Colour (0xff8a8ae6));

        // --- Detected pitch in Hz, useful for spotting octave errors ---
        bounds.removeFromTop (6);
        g.setColour (juce::Colour (0xff9a9aa8));
        g.setFont (juce::FontOptions (13.0f));
        const juce::String pitchText = features.pitchHz > 0.0f
            ? juce::String (features.pitchHz, 1) + " Hz"
            : juce::String ("--");
        g.drawText ("Detected pitch: " + pitchText, bounds.removeFromTop (20),
                     juce::Justification::centredLeft);
    }

private:
    void timerCallback() override { repaint(); }

    static juce::String midiNoteName (int note)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                        "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
    }

    void drawMeter (juce::Graphics& g, juce::Rectangle<int> area,
                     const juce::String& label, float value, juce::Colour colour)
    {
        area = area.reduced (0, 4);
        auto labelArea = area.removeFromLeft (86);

        g.setColour (juce::Colour (0xff9a9aa8));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (label, labelArea, juce::Justification::centredLeft);

        auto valueArea = area.removeFromRight (44);
        g.drawText (juce::String (value, 2), valueArea, juce::Justification::centredRight);

        auto barArea = area.reduced (4, 5);
        g.setColour (juce::Colour (0xff2e2e36));
        g.fillRoundedRectangle (barArea.toFloat(), 3.0f);

        const float clamped = juce::jlimit (0.0f, 1.0f, value);
        if (clamped > 0.0f)
        {
            auto filled = barArea.toFloat().withWidth (barArea.getWidth() * clamped);
            g.setColour (colour);
            g.fillRoundedRectangle (filled, 3.0f);
        }
    }

    ExpressionSynthProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExpressionSynthEditor)
};
