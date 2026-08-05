#pragma once
#include "PluginProcessor.h"
#include "UIComponents.h"
#include "ExpressionMapper.h"

// Thin track, small round grip. Deliberately quiet so the meters and the
// spectrum stay the loudest thing on screen.
class RowLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPos, float, float,
                            juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cy = (float) y + (float) height * 0.5f;

        g.setColour (juce::Colour (0xff2a2f3a));
        g.fillRect ((float) x, cy - 1.0f, (float) width, 2.0f);

        g.setColour (Palette::bone);
        g.fillEllipse (sliderPos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
    }
};

// One parameter: name on the left, slider in the middle, value on the right.
class ParamRow : public juce::Component
{
public:
    ParamRow (juce::AudioProcessorValueTreeState& s, juce::String id, juce::String labelText)
        : state (s), paramID (std::move (id)), label (std::move (labelText))
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setLookAndFeel (&lnf);
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
                        (state, paramID, slider);
    }

    ~ParamRow() override { slider.setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        drawLabel (g, label, r.removeFromLeft (92), Palette::muted, 10.0f);

        juce::String text;
        if (auto* p = state.getParameter (paramID))
            text = p->getCurrentValueAsText();

        g.setColour (juce::Colour (0xff98a0b0));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (text, r.removeFromRight (56), juce::Justification::centredRight);
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds().withTrimmedLeft (96).withTrimmedRight (60));
    }

    static constexpr int preferredHeight = 26;

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String paramID, label;
    juce::Slider slider;
    RowLookAndFeel lnf;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

inline void drawGroupHeader (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
{
    g.setColour (Palette::line);
    g.drawHorizontalLine (area.getY(), (float) area.getX(), (float) area.getRight());
    drawLabel (g, text, area.withTrimmedTop (10).withHeight (12), Palette::dim, 9.0f);
}

// ---------------------------------------------------------------------
// DETECT: everything about getting the plugin to trigger correctly.
// ---------------------------------------------------------------------
class DetectPane : public juce::Component
{
public:
    DetectPane (ExpressionSynthProcessor& p)
        : proc (p), spectrum (p.getSpectrum()),
          ampMeter ("Amplitude", Palette::amp),
          confMeter ("Pitch confidence", Palette::conf),
          onsetMeter ("Onset", Palette::onset)
    {
        auto& s = proc.getParams();

        addAndMakeVisible (spectrum);
        addAndMakeVisible (adaptiveSwitch);
        addAndMakeVisible (pad);
        addAndMakeVisible (weightBars);
        addAndMakeVisible (articulation);
        addAndMakeVisible (ampMeter);
        addAndMakeVisible (confMeter);
        addAndMakeVisible (onsetMeter);
        addAndMakeVisible (mode);

        articulation.setOptions (ArticulationProfile::getPresetNames());
        if (auto* p2 = s.getParameter (SynthEngine::articulationPresetParamID))
            articulation.setSelected ((int) (p2->getValue() * 4.0f + 0.5f));

        articulation.onSelect = [this, &s] (int i)
        {
            if (auto* p3 = s.getParameter (SynthEngine::articulationPresetParamID))
            {
                p3->beginChangeGesture();
                p3->setValueNotifyingHost ((float) i / 4.0f);
                p3->endChangeGesture();
            }
        };

        // Release marker is secondary: the hysteresis pair share one
        // track so the gap between them is visible as a thing, not a
        // relationship you have to hold in your head.
        ampMeter.addMarker (s.getParameter (SynthEngine::releaseThresholdParamID), "release", false);
        ampMeter.addMarker (s.getParameter (SynthEngine::onsetThresholdParamID), "onset", true);
        confMeter.addMarker (s.getParameter (SynthEngine::confidenceGateParamID), "gate", true);
        onsetMeter.addMarker (s.getParameter (SynthEngine::retriggerSensParamID), "retrigger", true);

        mode.setOptions ({ "Quantised", "Glide" });
        if (auto* g = s.getParameter (SynthEngine::glideModeParamID))
            mode.setSelected (g->getValue() > 0.5f ? 1 : 0);

        mode.onSelect = [&s] (int i)
        {
            if (auto* g = s.getParameter (SynthEngine::glideModeParamID))
            {
                g->beginChangeGesture();
                g->setValueNotifyingHost (i == 1 ? 1.0f : 0.0f);
                g->endChangeGesture();
            }
        };

        if (auto* a = s.getParameter (SynthEngine::adaptiveParamID))
            adaptiveSwitch.setState (a->getValue() > 0.5f);

        adaptiveSwitch.onToggle = [this, &s] (bool on)
        {
            if (auto* a = s.getParameter (SynthEngine::adaptiveParamID))
            {
                a->beginChangeGesture();
                a->setValueNotifyingHost (on ? 1.0f : 0.0f);
                a->endChangeGesture();
            }
            applyMode (on);
        };

        adaptRate = std::make_unique<ParamRow> (s, SynthEngine::adaptRateParamID, "Adapt over");
        addAndMakeVisible (*adaptRate);

        stability = std::make_unique<ParamRow> (s, SynthEngine::pitchStabilityParamID, "Stability");
        bendRange = std::make_unique<ParamRow> (s, SynthEngine::bendRangeParamID, "Bend range");
        addAndMakeVisible (*stability);
        addAndMakeVisible (*bendRange);

        applyMode (adaptiveSwitch.getState());
    }

    // Manual and adaptive occupy the same space: one shows the five-way
    // selector, the other the descriptor pad. Nothing is stacked, so the
    // pane stays the same length either way.
    void applyMode (bool adaptive)
    {
        pad.setVisible (adaptive);
        weightBars.setVisible (adaptive);
        adaptRate->setVisible (adaptive);
        articulation.setVisible (! adaptive);

        ampMeter.setLocked (adaptive);
        confMeter.setLocked (adaptive);
        onsetMeter.setLocked (adaptive);

        if (! adaptive)
        {
            ampMeter.setMarkerOverride (0, -1.0f);
            ampMeter.setMarkerOverride (1, -1.0f);
            confMeter.setMarkerOverride (0, -1.0f);
            onsetMeter.setMarkerOverride (0, -1.0f);
        }

        resized();
        repaint();
    }

    void refresh()
    {
        const auto f = proc.getFeaturesForDisplay();
        const bool sounding = proc.getActiveNoteForDisplay() >= 0;

        const bool adaptive = proc.isAdaptive();
        if (adaptive != lastAdaptive)
        {
            adaptiveSwitch.setState (adaptive);
            applyMode (adaptive);
            lastAdaptive = adaptive;
        }

        if (adaptive)
        {
            const auto& an = proc.getAnalyser();
            const auto d = an.getDescriptors();

            pad.setPosition (d.sustain, d.sharpness, an.getWeights());
            weightBars.setWeights (an.getWeights());

            // Show where the thresholds have actually moved to.
            const auto& live = an.getProfile();
            ampMeter.setMarkerOverride (0, live.releaseAmplitudeThreshold);
            ampMeter.setMarkerOverride (1, live.onsetAmplitudeThreshold);
            confMeter.setMarkerOverride (0, live.minPitchConfidence);
            onsetMeter.setMarkerOverride (0, live.onsetRetriggerThreshold);

            pad.repaint();
            weightBars.repaint();
        }

        ampMeter.setValue (f.amplitude);
        confMeter.setValue (f.pitchConfidence);
        confMeter.setBarColour (f.pitchConfidence >= confMeter.getPrimaryThreshold()
                                  ? Palette::conf : Palette::confLow);
        onsetMeter.setValue (f.onsetStrength);

        // Centroid arrives normalised against Nyquist; the display axis
        // is logarithmic and tops out at 8 kHz, so convert.
        const float centroidHz = f.spectralCentroid * (float) proc.getSampleRate() * 0.5f;
        spectrum.setReadings (SpectrumData::positionForHz (centroidHz), f.pitchHz, sounding);

        pitchText = sounding && f.pitchHz > 0.0f
            ? juce::String (f.pitchHz, 1) + " Hz" : juce::String ("--");

        spectrum.repaint();
        ampMeter.repaint();
        confMeter.repaint();
        onsetMeter.repaint();
        repaint (legendArea);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Palette::dim);
        g.setFont (juce::FontOptions (9.0f).withStyle ("Bold"));
        g.drawText ("CENTROID   PITCH   PEAK   FLOOR        50 HZ - 8 KHZ  |  " + pitchText,
                     legendArea, juce::Justification::centredLeft);
        drawGroupHeader (g, "Pitch handling", groupArea);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 12);

        spectrum.setBounds (r.removeFromTop (104));
        legendArea = r.removeFromTop (18);
        r.removeFromTop (6);

        adaptiveSwitch.setBounds (r.removeFromTop (24));
        r.removeFromTop (8);

        if (adaptiveSwitch.getState())
        {
            auto padRow = r.removeFromTop (118);
            weightBars.setBounds (padRow.removeFromRight (132).reduced (4, 6));
            pad.setBounds (padRow.withTrimmedRight (8));
            r.removeFromTop (6);
            adaptRate->setBounds (r.removeFromTop (ParamRow::preferredHeight));
        }
        else
        {
            articulation.setBounds (r.removeFromTop (32));
            r.removeFromTop (ParamRow::preferredHeight + 92);
        }

        r.removeFromTop (12);

        ampMeter.setBounds (r.removeFromTop (ThresholdMeter::preferredHeight));
        r.removeFromTop (10);
        confMeter.setBounds (r.removeFromTop (ThresholdMeter::preferredHeight));
        r.removeFromTop (10);
        onsetMeter.setBounds (r.removeFromTop (ThresholdMeter::preferredHeight));
        r.removeFromTop (6);

        groupArea = r.removeFromTop (34);
        stability->setBounds (r.removeFromTop (ParamRow::preferredHeight));
        bendRange->setBounds (r.removeFromTop (ParamRow::preferredHeight));
        r.removeFromTop (8);
        mode.setBounds (r.removeFromTop (32));
    }

    static constexpr int contentHeight = 620;

private:
    ExpressionSynthProcessor& proc;
    SpectrumDisplay spectrum;
    SegmentedControl articulation, mode;
    ToggleSwitch adaptiveSwitch;
    DescriptorPad pad;
    WeightBars weightBars;
    ThresholdMeter ampMeter, confMeter, onsetMeter;
    std::unique_ptr<ParamRow> stability, bendRange, adaptRate;
    bool lastAdaptive = false;
    juce::Rectangle<int> legendArea, groupArea;
    juce::String pitchText { "--" };
};

// ---------------------------------------------------------------------
// SYNTH: the engine.
// ---------------------------------------------------------------------
class SynthPane : public juce::Component
{
public:
    SynthPane (ExpressionSynthProcessor& p) : proc (p)
    {
        auto& s = proc.getParams();
        addAndMakeVisible (scope);

        auto add = [this, &s] (const char* id, const char* label)
        {
            auto row = std::make_unique<ParamRow> (s, id, label);
            addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        };

        add (SynthEngine::audioNotesParamID,    "Audio notes");
        add (SynthEngine::midiNotesParamID,     "MIDI notes");

        add (SynthEngine::syncMixParamID,       "Sync mix");
        add (SynthEngine::syncRatioParamID,     "Ratio");
        add (SynthEngine::syncSensParamID,      "Sensitivity");
        add (SynthEngine::syncReleaseParamID,   "Release");

        add (SynthEngine::morphParamID,         "Waveshape");
        add (SynthEngine::morphModDepthParamID, "Mod depth");
        add (SynthEngine::unisonParamID,        "Voices");
        add (SynthEngine::detuneParamID,        "Detune");
        add (SynthEngine::spreadParamID,        "Spread");
        add (SynthEngine::cutoffParamID,        "Cutoff");
        add (SynthEngine::resonanceParamID,     "Resonance");
        add (SynthEngine::cutoffModDepthParamID,"Mod depth");
        add (SynthEngine::attackParamID,        "Attack");
        add (SynthEngine::decayParamID,         "Decay");
        add (SynthEngine::sustainParamID,       "Sustain");
        add (SynthEngine::releaseParamID,       "Release");
        add (SynthEngine::ampLevelParamID,      "Level");
    }

    void refresh()
    {
        auto& s = proc.getParams();

        // Say plainly what the current mix means, since the two layers
        // behave so differently that a bare number is not enough.
        const bool audioNotes = s.getRawParameterValue (SynthEngine::audioNotesParamID)->load() > 0.5f;
        const bool midiNotes  = s.getRawParameterValue (SynthEngine::midiNotesParamID)->load() > 0.5f;

        juce::String sHint;
        if (audioNotes && midiNotes)
            sHint = "Both. Detected notes carry the input's envelope and bend; keyboard notes "
                    "play at their own velocity. Timbre follows the input either way.";
        else if (audioNotes)
            sHint = "The instrument plays the synth.";
        else if (midiNotes)
            sHint = "Keyboard plays the notes; the instrument shapes their timbre.";
        else
            sHint = "No note source enabled - nothing will sound.";

        if (sHint != sourceHint)
        {
            sourceHint = sHint;
            repaint (sourceNote);
        }

        const float mix = proc.getSyncMix();
        juce::String hint;

        if (mix < 0.001f)
            hint = "Note mode. Pitch is detected and quantised; adaptive articulation applies.";
        else if (mix > 0.999f)
            hint = "Hard sync only. Responds within one cycle, but there are no notes - "
                   "articulation, glide and the envelope do nothing.";
        else
            hint = "Blended. Sync responds immediately and covers the detection delay; "
                   "the note layer arrives beneath it.";

        if (hint != syncHint)
        {
            syncHint = hint;
            repaint (syncNote);
        }

        const float base = s.getRawParameterValue (SynthEngine::morphParamID)->load();
        const float depth = s.getRawParameterValue (SynthEngine::morphModDepthParamID)->load();
        const float live = juce::jlimit (0.0f, 1.0f,
                                          base + proc.getFeaturesForDisplay().spectralCentroid * depth);
        scope.setPositions (base, live);
        scope.repaint();
    }

    void paint (juce::Graphics& g) override
    {
        for (size_t i = 0; i < headers.size(); ++i)
            drawGroupHeader (g, headers[i].first, headers[i].second);

        g.setColour (Palette::dim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawFittedText (sourceHint, sourceNote, juce::Justification::topLeft, 2);
        g.drawFittedText (syncHint, syncNote, juce::Justification::topLeft, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 12);
        headers.clear();

        drawLabelSlot (r, "Note sources");
        place (r, 0, 2);
        sourceNote = r.removeFromTop (30);

        drawLabelSlot (r, "Sync");
        place (r, 2, 6);
        syncNote = r.removeFromTop (30);

        drawLabelSlot (r, "Oscillator");
        scope.setBounds (r.removeFromTop (78));
        r.removeFromTop (8);
        place (r, 6, 8);

        drawLabelSlot (r, "Unison");
        place (r, 8, 11);

        drawLabelSlot (r, "Filter");
        place (r, 11, 14);

        drawLabelSlot (r, "Envelope");
        place (r, 14, 18);

        drawLabelSlot (r, "Output");
        place (r, 18, 19);
    }

    static constexpr int contentHeight = 980;

private:
    void drawLabelSlot (juce::Rectangle<int>& r, const juce::String& name)
    {
        headers.push_back ({ name, r.removeFromTop (32) });
    }

    void place (juce::Rectangle<int>& r, size_t from, size_t to)
    {
        for (size_t i = from; i < to && i < rows.size(); ++i)
            rows[i]->setBounds (r.removeFromTop (ParamRow::preferredHeight));
        r.removeFromTop (8);
    }

    ExpressionSynthProcessor& proc;
    WaveScope scope;
    std::vector<std::unique_ptr<ParamRow>> rows;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> headers;
    juce::Rectangle<int> syncNote, sourceNote;
    juce::String syncHint, sourceHint;
};

// ---------------------------------------------------------------------
// MATRIX: what drives what, and now editable.
//
// Six slots, each four parameters. Routes were made data rather than code
// from the outset so that this pane could exist; reading them from
// parameters means editing here needs no locking against the audio
// thread, and the host saves them with the patch for free.
// ---------------------------------------------------------------------
class MatrixPane : public juce::Component
{
public:
    MatrixPane (ExpressionSynthProcessor& p) : proc (p)
    {
        auto& s = proc.getParams();

        for (int slot = 0; slot < ExpressionMapper::numSlots; ++slot)
        {
            auto add = [this, &s] (juce::String id, juce::String label)
            {
                auto row = std::make_unique<ParamRow> (s, id, label);
                addAndMakeVisible (*row);
                rows.push_back (std::move (row));
            };

            add (ExpressionMapper::sourceParamID (slot),      "Source");
            add (ExpressionMapper::destinationParamID (slot), "Destination");
            add (ExpressionMapper::depthParamID (slot),       "Depth");
            add (ExpressionMapper::curveParamID (slot),       "Curve");
        }
    }

    void paint (juce::Graphics& g) override
    {
        for (size_t i = 0; i < headers.size(); ++i)
            drawGroupHeader (g, headers[i].first, headers[i].second);

        g.setColour (Palette::dim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawFittedText ("Several slots may target one destination; they add. "
                           "A slot set to Off is inactive. Pitch bend already carries "
                           "the detected note's deviation, so a route to it adds on top.",
                           footer, juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 12);
        headers.clear();

        for (int slot = 0; slot < ExpressionMapper::numSlots; ++slot)
        {
            headers.push_back ({ "Route " + juce::String (slot + 1), r.removeFromTop (30) });

            for (int k = 0; k < 4; ++k)
            {
                const size_t index = (size_t) (slot * 4 + k);
                if (index < rows.size())
                    rows[index]->setBounds (r.removeFromTop (ParamRow::preferredHeight));
            }

            r.removeFromTop (6);
        }

        footer = r.removeFromTop (44);
    }

    static constexpr int contentHeight = 880;

private:
    ExpressionSynthProcessor& proc;
    std::vector<std::unique_ptr<ParamRow>> rows;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> headers;
    juce::Rectangle<int> footer;
};

// ---------------------------------------------------------------------
// Editor: persistent status strip, tabs, scrolling panes.
// ---------------------------------------------------------------------
class ExpressionSynthEditor : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit ExpressionSynthEditor (ExpressionSynthProcessor& p)
        : AudioProcessorEditor (&p), proc (p),
          detect (p), synth (p), matrix (p)
    {
        addAndMakeVisible (tabs);
        tabs.setOptions ({ "Detect", "Synth", "Matrix" });
        tabs.onSelect = [this] (int i) { showPane (i); };

        for (auto* v : { &detectView, &synthView, &matrixView })
        {
            addChildComponent (*v);
            v->setScrollBarsShown (true, false);
        }

        detectView.setViewedComponent (&detect, false);
        synthView.setViewedComponent (&synth, false);
        matrixView.setViewedComponent (&matrix, false);

        showPane (0);
        setSize (520, 600);
        startTimerHz (24);
    }

    ~ExpressionSynthEditor() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::ground);

        auto strip = getLocalBounds().removeFromTop (stripHeight);
        g.setColour (Palette::panel);
        g.fillRect (strip);
        g.setColour (Palette::line);
        g.drawHorizontalLine (strip.getBottom() - 1, 0.0f, (float) getWidth());

        auto inner = strip.reduced (16, 11);
        auto top = inner.removeFromTop (26);

        drawLabel (g, "ExpressionSynth", top, Palette::muted, 12.0f);

        const int note = proc.getActiveNoteForDisplay();
        const bool on = note >= 0;

        g.setColour (on ? Palette::conf : juce::Colour (0xff4a4f5c));
        g.setFont (juce::FontOptions (21.0f).withStyle ("Bold"));
        g.drawText (on ? noteName (note) : "--", top.removeFromRight (150),
                     juce::Justification::centredRight);

        inner.removeFromTop (5);
        auto bars = inner.removeFromTop (16);
        const auto f = proc.getFeaturesForDisplay();

        drawStripMeter (g, bars.removeFromLeft (bars.getWidth() / 2 - 7),
                         "Amplitude", f.amplitude, Palette::amp);
        bars.removeFromLeft (14);
        drawStripMeter (g, bars, "Confidence", f.pitchConfidence,
                         f.pitchConfidence >= 0.5f ? Palette::conf : Palette::confLow);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (stripHeight);

        tabs.setBounds (r.removeFromTop (36).reduced (12, 3));

        for (auto* v : { &detectView, &synthView, &matrixView })
            v->setBounds (r);

        detect.setSize (r.getWidth() - 10, DetectPane::contentHeight);
        synth.setSize  (r.getWidth() - 10, SynthPane::contentHeight);
        matrix.setSize (r.getWidth() - 10, MatrixPane::contentHeight);
    }

private:
    static constexpr int stripHeight = 64;

    void showPane (int index)
    {
        detectView.setVisible (index == 0);
        synthView.setVisible (index == 1);
        matrixView.setVisible (index == 2);
        current = index;
    }

    void timerCallback() override
    {
        repaint (getLocalBounds().removeFromTop (stripHeight));

        if (current == 0) detect.refresh();
        else if (current == 1) synth.refresh();
    }

    static void drawStripMeter (juce::Graphics& g, juce::Rectangle<int> area,
                                 const juce::String& label, float value, juce::Colour colour)
    {
        drawLabel (g, label, area.removeFromTop (10), Palette::muted, 9.0f);
        area.removeFromTop (3);

        auto bar = area.removeFromTop (3).toFloat();
        g.setColour (juce::Colour (0xff111318));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (colour);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (1.0f, bar.getWidth() * value)), 1.5f);
    }

    static juce::String noteName (int note)
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
    }

    ExpressionSynthProcessor& proc;
    SegmentedControl tabs;

    DetectPane detect;
    SynthPane synth;
    MatrixPane matrix;

    juce::Viewport detectView, synthView, matrixView;
    int current = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExpressionSynthEditor)
};
