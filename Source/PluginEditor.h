#pragma once
#include "PluginProcessor.h"
#include "UIComponents.h"
#include "ExpressionMapper.h"
#include "Arpeggiator.h"
#include "EffectsChain.h"

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
        drawLabel (g, label, r.removeFromLeft (labelWidth), Palette::muted, 10.0f);

        juce::String text;
        if (auto* p = state.getParameter (paramID))
            text = p->getCurrentValueAsText();

        g.setColour (juce::Colour (0xff98a0b0));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (text, r.removeFromRight (valueWidth), juce::Justification::centredRight);
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds()
                            .withTrimmedLeft (labelWidth + 4)
                            .withTrimmedRight (valueWidth + 4));
    }

    // Values now read musically, which makes them longer: "+12 st
    // (octave)" needs about twice the room a bare number did. A clipped
    // value is worse than the number it replaced.
    static constexpr int labelWidth = 86;
    static constexpr int valueWidth = 104;
    static constexpr int preferredHeight = 26;

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String paramID, label;
    juce::Slider slider;
    RowLookAndFeel lnf;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Rotary control, following the convention every synthesiser uses for a
// bounded amount: the arc shows position at a glance, and the pointer
// gives a precise reading. Bipolar parameters fill from the centre, so
// "no change" is visibly the middle rather than the far left.
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    bool bipolar = false;

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                            float pos, float startAngle, float endAngle,
                            juce::Slider&) override
    {
        auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float d = juce::jmin (area.getWidth(), area.getHeight());
        auto r = area.withSizeKeepingCentre (d, d).reduced (3.0f);
        const float radius = r.getWidth() * 0.5f;
        const auto centre = r.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float thickness = juce::jmax (3.0f, radius * 0.16f);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                              startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff2a2f3a));
        g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

        // Fill from the centre for bipolar controls, from the start
        // otherwise.
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                  juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (Palette::bone);
            g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }

        // Body and pointer.
        const float bodyR = radius - thickness * 1.15f;
        g.setColour (juce::Colour (0xff23262e));
        g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.2f, -bodyR + 2.0f, 2.4f, bodyR * 0.55f, 1.2f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                  .translated (centre.x, centre.y));
        g.setColour (Palette::bone);
        g.fillPath (pointer);
    }
};

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, juce::String id,
           juce::String labelText, bool bipolar = false)
        : state (s), paramID (std::move (id)), label (std::move (labelText))
    {
        lnf.bipolar = bipolar;
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (rotaryStart, rotaryEnd, true);
        slider.setLookAndFeel (&lnf);
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
                        (state, paramID, slider);
    }

    ~Knob() override { slider.setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        r.removeFromTop (knobSize);

        drawLabel (g, label, r.removeFromTop (13), Palette::muted, 9.0f,
                    juce::Justification::centred);

        juce::String text;
        if (auto* p = state.getParameter (paramID))
            text = p->getCurrentValueAsText();

        g.setColour (juce::Colour (0xff98a0b0));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (text, r, juce::Justification::centredTop);
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds().removeFromTop (knobSize));
    }

    static constexpr int knobSize = 54;
    static constexpr int cellWidth = 116;
    static constexpr int cellHeight = knobSize + 13 + 14;

private:
    // Just under three quarters of a turn, the usual span - enough travel
    // for fine adjustment without the pointer ever pointing straight up
    // at two different values.
    static constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.25f;
    static constexpr float rotaryEnd   = juce::MathConstants<float>::pi * 2.75f;

    juce::AudioProcessorValueTreeState& state;
    juce::String paramID, label;
    juce::Slider slider;
    KnobLookAndFeel lnf;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Vertical fader. Used for envelope stages, where four side by side form
// a picture of the envelope itself - which is why every synthesiser
// draws them this way rather than as four knobs.
class FaderLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPos, float, float,
                            juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cx = (float) x + (float) width * 0.5f;

        g.setColour (juce::Colour (0xff101218));
        g.fillRoundedRectangle (cx - 3.0f, (float) y, 6.0f, (float) height, 3.0f);

        g.setColour (Palette::bone.withAlpha (0.55f));
        g.fillRoundedRectangle (cx - 3.0f, sliderPos, 6.0f,
                                 (float) (y + height) - sliderPos, 3.0f);

        g.setColour (Palette::bone);
        g.fillRoundedRectangle (cx - 11.0f, sliderPos - 5.0f, 22.0f, 10.0f, 3.0f);
        g.setColour (juce::Colour (0xff23262e));
        g.fillRect (cx - 9.0f, sliderPos - 0.5f, 18.0f, 1.0f);
    }
};

class Fader : public juce::Component
{
public:
    Fader (juce::AudioProcessorValueTreeState& s, juce::String id, juce::String labelText)
        : state (s), paramID (std::move (id)), label (std::move (labelText))
    {
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setLookAndFeel (&lnf);
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
                        (state, paramID, slider);
    }

    ~Fader() override { slider.setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        r.removeFromTop (trackHeight);

        drawLabel (g, label, r.removeFromTop (13), Palette::muted, 9.0f,
                    juce::Justification::centred);

        juce::String text;
        if (auto* p = state.getParameter (paramID))
            text = p->getCurrentValueAsText();

        g.setColour (juce::Colour (0xff98a0b0));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (text, r, juce::Justification::centredTop);
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds().removeFromTop (trackHeight).reduced (0, 6));
    }

    static constexpr int trackHeight = 104;
    static constexpr int cellWidth = 62;
    static constexpr int cellHeight = trackHeight + 13 + 14;

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String paramID, label;
    juce::Slider slider;
    FaderLookAndFeel lnf;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A choice parameter deserves a menu, not a slider you drag until the
// right word appears. Dragging works, but it makes the user hunt for a
// value they can already name.
class ChoiceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ChoiceLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, Palette::well);
        setColour (juce::ComboBox::textColourId, Palette::bone);
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff23262e));
        setColour (juce::ComboBox::arrowColourId, Palette::muted);
        setColour (juce::PopupMenu::backgroundColourId, Palette::panel);
        setColour (juce::PopupMenu::textColourId, Palette::bone);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::panel2);
        setColour (juce::PopupMenu::highlightedTextColourId, Palette::bone);
    }

    // Inside an AUv3 the plugin is a view in the host's window, and iOS
    // will not let the extension open a window of its own. A popup menu
    // opened as a desktop window therefore never appears in Cubasis,
    // although the standalone app shows it. Parenting the menu to the
    // editor keeps it inside the plugin's own view, which works in both.
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (
            juce::ComboBox& box, juce::Label& label) override
    {
        auto opts = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label);

        if (auto* editor = box.findParentComponentOfClass<juce::AudioProcessorEditor>())
            opts = opts.withParentComponent (editor);

        return opts;
    }
};

class ChoiceRow : public juce::Component
{
public:
    ChoiceRow (juce::AudioProcessorValueTreeState& s, juce::String id, juce::String labelText)
        : label (std::move (labelText))
    {
        combo.setLookAndFeel (&lnf);
        combo.setJustificationType (juce::Justification::centredLeft);

        // Options come from the parameter itself, so a name added in the
        // engine appears here without a second list to keep in step.
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
            combo.addItemList (choice->choices, 1);

        addAndMakeVisible (combo);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
                        (s, id, combo);
    }

    ~ChoiceRow() override { combo.setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        drawLabel (g, label, getLocalBounds().removeFromLeft (ParamRow::labelWidth),
                    Palette::muted, 10.0f);
    }

    void resized() override
    {
        combo.setBounds (getLocalBounds()
                            .withTrimmedLeft (ParamRow::labelWidth + 4)
                            .reduced (0, 2));
    }

    static constexpr int preferredHeight = 30;

private:
    juce::String label;
    juce::ComboBox combo;
    ChoiceLookAndFeel lnf;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// An on/off parameter gets a switch. As a slider it reads as a
// continuous control that happens to have two positions.
class SwitchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                            bool, bool) override
    {
        const bool on = button.getToggleState();
        auto r = button.getLocalBounds().toFloat()
                    .withSizeKeepingCentre (38.0f, 21.0f)
                    .withX (0.0f);

        g.setColour (on ? juce::Colour (0xff2f6b4d) : Palette::well);
        g.fillRoundedRectangle (r, 10.5f);
        g.setColour (on ? juce::Colour (0xff3d8a63) : juce::Colour (0xff23262e));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.5f, 1.0f);

        g.setColour (Palette::bone);
        g.fillEllipse (r.getX() + (on ? 20.0f : 3.0f), r.getY() + 3.0f, 15.0f, 15.0f);
    }
};

class ToggleRow : public juce::Component
{
public:
    ToggleRow (juce::AudioProcessorValueTreeState& s, juce::String id, juce::String labelText)
        : label (std::move (labelText))
    {
        button.setLookAndFeel (&lnf);
        addAndMakeVisible (button);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                        (s, id, button);
    }

    ~ToggleRow() override { button.setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        drawLabel (g, label, getLocalBounds().removeFromLeft (ParamRow::labelWidth),
                    Palette::muted, 10.0f);
    }

    void resized() override
    {
        button.setBounds (getLocalBounds()
                            .withTrimmedLeft (ParamRow::labelWidth + 4)
                            .withWidth (44));
    }

    static constexpr int preferredHeight = 28;

private:
    juce::String label;
    juce::ToggleButton button;
    SwitchLookAndFeel lnf;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// Panes hold a mixed list: sliders for continuous values, menus for
// choices, switches for on/off. A single vector of Components keeps the
// layout code from having to know which is which.
// Knobs and faders sit in rows across the panel rather than stacked one
// per line, which is how every hardware and software synthesiser lays
// them out - and it fits far more in the same height.
struct ControlGrid
{
    std::vector<std::unique_ptr<juce::Component>> cells;
    std::vector<bool> isFader;

    void addKnob (juce::Component& owner, juce::AudioProcessorValueTreeState& s,
                   juce::String id, juce::String label, bool bipolar = false)
    {
        auto k = std::make_unique<Knob> (s, id, label, bipolar);
        owner.addAndMakeVisible (*k);
        cells.push_back (std::move (k));
        isFader.push_back (false);
    }

    void addFader (juce::Component& owner, juce::AudioProcessorValueTreeState& s,
                    juce::String id, juce::String label)
    {
        auto f = std::make_unique<Fader> (s, id, label);
        owner.addAndMakeVisible (*f);
        cells.push_back (std::move (f));
        isFader.push_back (true);
    }

    // Lays cells [from, to) across the width, wrapping as needed, and
    // consumes exactly the height used.
    void layout (juce::Rectangle<int>& area, size_t from, size_t to, int perRow) const
    {
        const int count = (int) (juce::jmin (to, cells.size()) - from);
        if (count <= 0)
            return;

        const bool faders = isFader[from];
        const int cw = faders ? Fader::cellWidth : Knob::cellWidth;
        const int ch = faders ? Fader::cellHeight : Knob::cellHeight;
        const int columns = juce::jmax (1, juce::jmin (perRow, count));

        size_t i = from;
        while (i < from + (size_t) count)
        {
            auto row = area.removeFromTop (ch);
            const int n = (int) juce::jmin ((size_t) columns, from + (size_t) count - i);

            // Centre each row so a short final row does not sit oddly
            // against the left edge.
            const int used = n * cw;
            row.removeFromLeft (juce::jmax (0, (row.getWidth() - used) / 2));

            for (int c = 0; c < n; ++c, ++i)
                cells[i]->setBounds (row.removeFromLeft (cw));

            area.removeFromTop (6);
        }
    }

    static int heightFor (int count, int perRow, bool faders)
    {
        if (count <= 0) return 0;
        const int rows = (count + perRow - 1) / perRow;
        const int ch = faders ? Fader::cellHeight : Knob::cellHeight;
        return rows * (ch + 6);
    }
};

struct RowList
{
    std::vector<std::unique_ptr<juce::Component>> rows;
    std::vector<int> heights;

    void addSlider (juce::Component& owner, juce::AudioProcessorValueTreeState& s,
                     juce::String id, juce::String label)
    {
        auto r = std::make_unique<ParamRow> (s, id, label);
        owner.addAndMakeVisible (*r);
        heights.push_back (ParamRow::preferredHeight);
        rows.push_back (std::move (r));
    }

    void addChoice (juce::Component& owner, juce::AudioProcessorValueTreeState& s,
                     juce::String id, juce::String label)
    {
        auto r = std::make_unique<ChoiceRow> (s, id, label);
        owner.addAndMakeVisible (*r);
        heights.push_back (ChoiceRow::preferredHeight);
        rows.push_back (std::move (r));
    }

    void addToggle (juce::Component& owner, juce::AudioProcessorValueTreeState& s,
                     juce::String id, juce::String label)
    {
        auto r = std::make_unique<ToggleRow> (s, id, label);
        owner.addAndMakeVisible (*r);
        heights.push_back (ToggleRow::preferredHeight);
        rows.push_back (std::move (r));
    }

    // Lays out rows [from, to) and returns the height consumed.
    int place (juce::Rectangle<int>& area, size_t from, size_t to) const
    {
        int used = 0;
        for (size_t i = from; i < to && i < rows.size(); ++i)
        {
            rows[i]->setBounds (area.removeFromTop (heights[i]));
            used += heights[i];
        }
        area.removeFromTop (8);
        return used + 8;
    }

    int totalHeight() const
    {
        int t = 0;
        for (int h : heights) t += h;
        return t;
    }
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

        grid.addKnob (*this, s, SynthEngine::adaptRateParamID,     "Adapt over");
        grid.addKnob (*this, s, SynthEngine::pitchMinParamID,      "Lowest");
        grid.addKnob (*this, s, SynthEngine::pitchMaxParamID,      "Highest");
        grid.addKnob (*this, s, SynthEngine::pitchStabilityParamID,"Stability");
        grid.addKnob (*this, s, SynthEngine::bendRangeParamID,     "Bend");


        applyMode (adaptiveSwitch.getState());
    }

    // Manual and adaptive occupy the same space: one shows the five-way
    // selector, the other the descriptor pad. Nothing is stacked, so the
    // pane stays the same length either way.
    void applyMode (bool adaptive)
    {
        pad.setVisible (adaptive);
        weightBars.setVisible (adaptive);
        grid.cells[0]->setVisible (adaptive);      // adapt rate
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
            grid.layout (r, 0, 1, 1);
        }
        else
        {
            articulation.setBounds (r.removeFromTop (32));
            r.removeFromTop (Knob::cellHeight + 98);
        }

        r.removeFromTop (12);

        ampMeter.setBounds (r.removeFromTop (ThresholdMeter::preferredHeight));
        r.removeFromTop (10);
        confMeter.setBounds (r.removeFromTop (ThresholdMeter::preferredHeight));
        r.removeFromTop (10);
        onsetMeter.setBounds (r.removeFromTop (ThresholdMeter::preferredHeight));
        r.removeFromTop (6);

        groupArea = r.removeFromTop (34);
        grid.layout (r, 1, 5, 4);
        r.removeFromTop (4);
        mode.setBounds (r.removeFromTop (32));
    }

    static constexpr int contentHeight = 760;

private:
    ExpressionSynthProcessor& proc;
    SpectrumDisplay spectrum;
    SegmentedControl articulation, mode;
    ToggleSwitch adaptiveSwitch;
    DescriptorPad pad;
    WeightBars weightBars;
    ThresholdMeter ampMeter, confMeter, onsetMeter;
    ControlGrid grid;
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

        rows.addToggle (*this, s, SynthEngine::audioNotesParamID, "Audio notes");
        rows.addToggle (*this, s, SynthEngine::midiNotesParamID,  "MIDI notes");
        rows.addChoice (*this, s, SynthEngine::oscModeParamID,    "Mode");

        for (int i = 0; i < SynthEngine::numOscSlots; ++i)
            rows.addChoice (*this, s, SynthEngine::oscShapeParamID (i),
                             "Osc " + juce::String (i + 1));

        grid.addKnob (*this, s, SynthEngine::syncMixParamID,     "Mix");
        grid.addKnob (*this, s, SynthEngine::syncRatioParamID,   "Ratio");
        grid.addKnob (*this, s, SynthEngine::syncSensParamID,    "Sens");
        grid.addKnob (*this, s, SynthEngine::syncReleaseParamID, "Release");

        grid.addKnob (*this, s, SynthEngine::morphParamID,         "Shape");
        grid.addKnob (*this, s, SynthEngine::morphModDepthParamID, "Mod");
        grid.addKnob (*this, s, SynthEngine::unisonParamID,        "Voices");
        grid.addKnob (*this, s, SynthEngine::detuneParamID,        "Detune");
        grid.addKnob (*this, s, SynthEngine::spreadParamID,        "Spread");

        for (int i = 0; i < SynthEngine::numOscSlots; ++i)
        {
            grid.addKnob (*this, s, SynthEngine::oscOctaveParamID (i), "Octave", true);
            grid.addKnob (*this, s, SynthEngine::oscDetuneParamID (i), "Detune", true);
            grid.addKnob (*this, s, SynthEngine::oscLevelParamID (i),  "Level");
            grid.addKnob (*this, s, SynthEngine::oscPanParamID (i),    "Pan", true);
        }

        grid.addKnob (*this, s, SynthEngine::cutoffParamID,         "Cutoff");
        grid.addKnob (*this, s, SynthEngine::resonanceParamID,      "Reso");
        grid.addKnob (*this, s, SynthEngine::cutoffModDepthParamID, "Mod");

        // Four faders side by side draw the envelope's own shape, which
        // is why every synthesiser lays an ADSR out this way.
        grid.addFader (*this, s, SynthEngine::attackParamID,  "A");
        grid.addFader (*this, s, SynthEngine::decayParamID,   "D");
        grid.addFader (*this, s, SynthEngine::sustainParamID, "S");
        grid.addFader (*this, s, SynthEngine::releaseParamID, "R");

        grid.addKnob (*this, s, SynthEngine::ampLevelParamID, "Level");
    }

    void refresh()
    {
        auto& s = proc.getParams();

        if (proc.isStackMode() != lastStack)
        {
            lastStack = proc.isStackMode();
            resized();
            repaint();
        }

        const bool audioNotes = s.getRawParameterValue (SynthEngine::audioNotesParamID)->load() > 0.5f;
        const bool midiNotes  = s.getRawParameterValue (SynthEngine::midiNotesParamID)->load() > 0.5f;

        juce::String sHint;
        if (audioNotes && midiNotes)
            sHint = "Both. Detected notes carry the input's envelope and bend; keyboard notes "
                    "play at their own velocity. Timbre follows the input either way.";
        else if (audioNotes) sHint = "The instrument plays the synth.";
        else if (midiNotes)  sHint = "Keyboard plays the notes; the instrument shapes their timbre.";
        else                 sHint = "No note source enabled - nothing will sound.";

        if (sHint != sourceHint) { sourceHint = sHint; repaint (sourceNote); }

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

        if (hint != syncHint) { syncHint = hint; repaint (syncNote); }

        if (! lastStack)
        {
            const float base = s.getRawParameterValue (SynthEngine::morphParamID)->load();
            const float depth = s.getRawParameterValue (SynthEngine::morphModDepthParamID)->load();
            const float live = juce::jlimit (0.0f, 1.0f,
                base + proc.getFeaturesForDisplay().spectralCentroid * depth);
            scope.setPositions (base, live);
            scope.repaint();
        }
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

        const bool stack = proc.isStackMode();

        for (size_t i = 3; i < 7; ++i)   rows.rows[i]->setVisible (stack);
        for (size_t i = 4; i < 9; ++i)   grid.cells[i]->setVisible (! stack);
        for (size_t i = 9; i < 25; ++i)  grid.cells[i]->setVisible (stack);
        scope.setVisible (! stack);

        headers.push_back ({ "Note sources", r.removeFromTop (30) });
        rows.place (r, 0, 2);
        sourceNote = r.removeFromTop (30);

        headers.push_back ({ "Sync", r.removeFromTop (30) });
        grid.layout (r, 0, 4, 4);
        syncNote = r.removeFromTop (30);

        headers.push_back ({ "Oscillator", r.removeFromTop (30) });
        rows.place (r, 2, 3);

        if (! stack)
        {
            scope.setBounds (r.removeFromTop (78));
            r.removeFromTop (8);
            grid.layout (r, 4, 9, 3);
        }
        else
        {
            for (int slot = 0; slot < SynthEngine::numOscSlots; ++slot)
            {
                rows.place (r, (size_t) (3 + slot), (size_t) (4 + slot));
                grid.layout (r, (size_t) (9 + slot * 4), (size_t) (13 + slot * 4), 4);
            }
        }

        headers.push_back ({ "Filter", r.removeFromTop (30) });
        grid.layout (r, 25, 28, 3);

        headers.push_back ({ "Envelope", r.removeFromTop (30) });
        grid.layout (r, 28, 32, 4);

        headers.push_back ({ "Output", r.removeFromTop (30) });
        grid.layout (r, 32, 33, 1);
    }

    static constexpr int contentHeight = 1440;

private:
    ExpressionSynthProcessor& proc;
    WaveScope scope;
    RowList rows;
    ControlGrid grid;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> headers;
    juce::Rectangle<int> syncNote, sourceNote;
    juce::String syncHint, sourceHint;
    bool lastStack = false;
};

// ---------------------------------------------------------------------
// ARP: pattern generation.
//
// The note-source choice is the substantive control here, not the
// pattern: a conventional arpeggiator holds a chord, and monophonic
// detection supplies none, so where the notes come from is a real
// decision rather than a default.
// ---------------------------------------------------------------------
class ArpPane : public juce::Component
{
public:
    ArpPane (ExpressionSynthProcessor& p) : proc (p)
    {
        auto& s = proc.getParams();

        rows.addToggle (*this, s, Arpeggiator::enabledParamID, "Arp");
        rows.addChoice (*this, s, Arpeggiator::sourceParamID,  "Source");
        rows.addChoice (*this, s, Arpeggiator::chordParamID,   "Chord");
        rows.addChoice (*this, s, Arpeggiator::patternParamID, "Pattern");
        rows.addChoice (*this, s, Arpeggiator::rateParamID,    "Rate");

        grid.addKnob (*this, s, Arpeggiator::octavesParamID, "Octaves");
        grid.addKnob (*this, s, Arpeggiator::gateParamID,    "Gate");
        grid.addKnob (*this, s, Arpeggiator::swingParamID,   "Swing");
        grid.addKnob (*this, s, Arpeggiator::freeBpmParamID, "Free tempo");
    }

    void refresh()
    {
        auto& s = proc.getParams();
        const int src = (int) s.getRawParameterValue (Arpeggiator::sourceParamID)->load();

        juce::String h;
        if (src == 0)
            h = "Holds whatever is played on a keyboard. The instrument shapes the timbre "
                "of the pattern.";
        else if (src == 1)
            h = "Builds a chord upward from the detected note and arpeggiates it. One note "
                "in, a pattern out - Chord sets which.";
        else
            h = "Notes played into the plugin accumulate and are replayed as a pattern. "
                "The instrument still supplies every note. Stop for two seconds to clear.";

        if (h != hint) { hint = h; repaint (hintArea); }
    }

    void paint (juce::Graphics& g) override
    {
        for (size_t i = 0; i < headers.size(); ++i)
            drawGroupHeader (g, headers[i].first, headers[i].second);

        g.setColour (Palette::dim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawFittedText (hint, hintArea, juce::Justification::topLeft, 3);
        g.drawFittedText ("Free tempo is used only where the host provides none.",
                           tempoNote, juce::Justification::topLeft, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 12);
        headers.clear();

        headers.push_back ({ "Notes", r.removeFromTop (30) });
        rows.place (r, 0, 3);
        hintArea = r.removeFromTop (44);

        headers.push_back ({ "Pattern", r.removeFromTop (30) });
        rows.place (r, 3, 5);
        grid.layout (r, 0, 3, 3);

        headers.push_back ({ "Tempo", r.removeFromTop (30) });
        grid.layout (r, 3, 4, 1);
        tempoNote = r.removeFromTop (28);
    }

    static constexpr int contentHeight = 700;

private:
    ExpressionSynthProcessor& proc;
    RowList rows;
    ControlGrid grid;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> headers;
    juce::Rectangle<int> hintArea, tempoNote;
    juce::String hint;
};

// ---------------------------------------------------------------------
// FX: drive, delay, reverb.
// ---------------------------------------------------------------------
class FxPane : public juce::Component
{
public:
    FxPane (ExpressionSynthProcessor& p) : proc (p)
    {
        auto& s = proc.getParams();

        grid.addKnob (*this, s, EffectsChain::driveParamID,      "Drive");
        grid.addKnob (*this, s, EffectsChain::driveToneParamID,  "Tone");
        grid.addKnob (*this, s, EffectsChain::driveLevelParamID, "Level");
        grid.addKnob (*this, s, EffectsChain::driveMixParamID,   "Mix");

        rows.addToggle (*this, s, EffectsChain::delaySyncParamID,     "Sync");
        rows.addChoice (*this, s, EffectsChain::delayDivisionParamID, "Division");

        grid.addKnob (*this, s, EffectsChain::delayTimeParamID,     "Time");
        grid.addKnob (*this, s, EffectsChain::delayFeedbackParamID, "Feedback");
        grid.addKnob (*this, s, EffectsChain::delayDampParamID,     "Damping");
        grid.addKnob (*this, s, EffectsChain::delayMixParamID,      "Mix");

        grid.addKnob (*this, s, EffectsChain::reverbSizeParamID, "Size");
        grid.addKnob (*this, s, EffectsChain::reverbDampParamID, "Damping");
        grid.addKnob (*this, s, EffectsChain::reverbMixParamID,  "Mix");
    }

    void paint (juce::Graphics& g) override
    {
        for (size_t i = 0; i < headers.size(); ++i)
            drawGroupHeader (g, headers[i].first, headers[i].second);

        g.setColour (Palette::dim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawFittedText ("Drive, delay mix, delay feedback and reverb mix are all "
                           "matrix destinations, so the input's expression can reach them. "
                           "Level is separate from Drive because saturation raises the "
                           "signal as it compresses it.",
                           footer, juce::Justification::topLeft, 4);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 12);
        headers.clear();

        headers.push_back ({ "Drive", r.removeFromTop (30) });
        grid.layout (r, 0, 4, 4);

        headers.push_back ({ "Delay", r.removeFromTop (30) });
        rows.place (r, 0, 2);
        grid.layout (r, 4, 8, 4);

        headers.push_back ({ "Reverb", r.removeFromTop (30) });
        grid.layout (r, 8, 11, 3);

        footer = r.removeFromTop (58);
    }

    static constexpr int contentHeight = 700;

private:
    ExpressionSynthProcessor& proc;
    RowList rows;
    ControlGrid grid;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> headers;
    juce::Rectangle<int> footer;
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
            rows.addChoice (*this, s, ExpressionMapper::sourceParamID (slot),      "Source");
            rows.addChoice (*this, s, ExpressionMapper::destinationParamID (slot), "Destination");
            rows.addSlider (*this, s, ExpressionMapper::depthParamID (slot),       "Depth");
            rows.addChoice (*this, s, ExpressionMapper::curveParamID (slot),       "Curve");
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
            rows.place (r, (size_t) (slot * 4), (size_t) (slot * 4 + 4));
        }

        footer = r.removeFromTop (44);
    }

    static constexpr int contentHeight = 1060;

private:
    ExpressionSynthProcessor& proc;
    RowList rows;
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
          detect (p), synth (p), arp (p), fx (p), matrix (p)
    {
        addAndMakeVisible (tabs);
        tabs.setOptions ({ "Detect", "Synth", "Arp", "FX", "Matrix" });
        tabs.onSelect = [this] (int i) { showPane (i); };

        for (auto* v : { &detectView, &synthView, &arpView, &fxView, &matrixView })
        {
            addChildComponent (*v);
            v->setScrollBarsShown (true, false);
        }

        detectView.setViewedComponent (&detect, false);
        synthView.setViewedComponent (&synth, false);
        arpView.setViewedComponent (&arp, false);
        fxView.setViewedComponent (&fx, false);
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

        auto inner = strip.reduced (16, stripPadV);
        auto top = inner.removeFromTop (stripTopRow);

        drawLabel (g, "ExpressionSynth", top, Palette::muted, 12.0f);

        const int note = proc.getActiveNoteForDisplay();
        const bool on = note >= 0;

        g.setColour (on ? Palette::conf : juce::Colour (0xff4a4f5c));
        g.setFont (juce::FontOptions (21.0f).withStyle ("Bold"));
        g.drawText (on ? noteName (note) : "--", top.removeFromRight (150),
                     juce::Justification::centredRight);

        inner.removeFromTop (stripGap);
        auto bars = inner.removeFromTop (stripMeterH);
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

        for (auto* v : { &detectView, &synthView, &arpView, &fxView, &matrixView })
            v->setBounds (r);

        detect.setSize (r.getWidth() - 10, DetectPane::contentHeight);
        synth.setSize  (r.getWidth() - 10, SynthPane::contentHeight);
        arp.setSize    (r.getWidth() - 10, ArpPane::contentHeight);
        fx.setSize     (r.getWidth() - 10, FxPane::contentHeight);
        matrix.setSize (r.getWidth() - 10, MatrixPane::contentHeight);
    }

private:
    // Sizes derived from their parts rather than stated as a literal.
    // A hand-picked total that is a few pixels short does not fail or
    // warn - JUCE's removeFromTop simply returns whatever is left, and a
    // bar ends up zero pixels tall and invisible. Building the total from
    // the pieces makes that impossible.
    static constexpr int stripPadV      = 11;
    static constexpr int stripTopRow    = 26;   // wordmark and note name
    static constexpr int stripGap       = 5;
    static constexpr int stripLabelH    = 10;
    static constexpr int stripLabelGap  = 3;
    static constexpr int stripBarH      = 4;
    static constexpr int stripMeterH    = stripLabelH + stripLabelGap + stripBarH;

    static constexpr int stripHeight =
        stripPadV * 2 + stripTopRow + stripGap + stripMeterH;

    void showPane (int index)
    {
        detectView.setVisible (index == 0);
        synthView.setVisible (index == 1);
        arpView.setVisible (index == 2);
        fxView.setVisible (index == 3);
        matrixView.setVisible (index == 4);
        current = index;
    }

    void timerCallback() override
    {
        repaint (getLocalBounds().removeFromTop (stripHeight));

        if (current == 0) detect.refresh();
        else if (current == 1) synth.refresh();
        else if (current == 2) arp.refresh();
    }

    static void drawStripMeter (juce::Graphics& g, juce::Rectangle<int> area,
                                 const juce::String& label, float value, juce::Colour colour)
    {
        drawLabel (g, label, area.removeFromTop (stripLabelH), Palette::muted, 9.0f);
        area.removeFromTop (stripLabelGap);

        // Floor the height so that if this layout is ever short again the
        // bar is visibly wrong rather than invisible. A wrong bar gets
        // noticed; a missing one reads as a dead meter.
        auto bar = area.removeFromTop (juce::jmax (2, stripBarH)).toFloat();
        if (bar.getHeight() < 2.0f)
            bar = bar.withHeight (2.0f);
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
    ArpPane arp;
    FxPane fx;
    MatrixPane matrix;

    juce::Viewport detectView, synthView, arpView, fxView, matrixView;
    int current = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExpressionSynthEditor)
};
