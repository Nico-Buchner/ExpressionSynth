#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SpectrumData.h"
#include "MorphOscillator.h"

// Shared palette. The rule throughout: anything chromatic is data coming
// from the audio, anything bone-coloured is something the user sets.
namespace Palette
{
    const juce::Colour ground   { 0xff14161c };
    const juce::Colour panel    { 0xff1b1e26 };
    const juce::Colour panel2   { 0xff22262f };
    const juce::Colour line     { 0xff2c313c };
    const juce::Colour well     { 0xff101218 };
    const juce::Colour bone     { 0xffd9d5cc };
    const juce::Colour muted    { 0xff767d8d };
    const juce::Colour dim      { 0xff5d6474 };

    const juce::Colour amp      { 0xffe8a33d };
    const juce::Colour conf     { 0xff4fb87a };
    const juce::Colour confLow  { 0xffb8683a };
    const juce::Colour onset    { 0xffd8496b };
    const juce::Colour bright   { 0xff5aa8c8 };
    const juce::Colour noise    { 0xff8a7fc8 };
    const juce::Colour peak     { 0xffa9d4e4 };
}

inline void drawLabel (juce::Graphics& g, const juce::String& text,
                        juce::Rectangle<int> area, juce::Colour colour,
                        float size = 10.0f,
                        juce::Justification just = juce::Justification::centredLeft)
{
    g.setColour (colour);
    g.setFont (juce::FontOptions (size).withStyle ("Bold"));
    g.drawText (text.toUpperCase(), area, just);
}

// ---------------------------------------------------------------------
// Spectrum with peak hold, noise floor, centroid and detected pitch.
// ---------------------------------------------------------------------
class SpectrumDisplay : public juce::Component
{
public:
    explicit SpectrumDisplay (const SpectrumData& s) : data (s) {}

    void setReadings (float centroidNormalised, float pitchHz, bool noteSounding)
    {
        centroid = centroidNormalised;
        pitch = pitchHz;
        sounding = noteSounding;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Palette::well);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colour (0xff23262e));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);

        const float w = r.getWidth();
        const float h = r.getHeight();

        // Octave gridlines, so a logarithmic axis is actually readable.
        g.setColour (juce::Colour (0xff191c23));
        for (float f : { 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f })
        {
            const float x = SpectrumData::positionForHz (f) * w;
            g.drawVerticalLine ((int) x, 0.0f, h);
        }

        const float bandW = w / (float) SpectrumData::numBands;

        for (int i = 0; i < SpectrumData::numBands; ++i)
        {
            const float v = data.bands[i].load (std::memory_order_relaxed);
            if (v > 0.004f)
            {
                const float bh = magToHeight (v, h);
                g.setColour (Palette::bright.withAlpha (juce::jlimit (0.35f, 1.0f, 0.35f + v * 1.6f)));
                g.fillRect (i * bandW + 1.0f, h - bh, bandW - 2.0f, bh);
            }

            const float p = data.peaks[i].load (std::memory_order_relaxed);
            if (p > 0.004f)
            {
                const float ph = magToHeight (p, h);
                g.setColour (Palette::peak);
                g.fillRect (i * bandW + 1.0f, h - ph - 2.0f, bandW - 2.0f, 2.0f);
            }
        }

        const float floorLevel = data.noiseFloor.load (std::memory_order_relaxed);
        if (floorLevel > 0.0015f)
        {
            const float y = h - magToHeight (floorLevel, h);
            g.setColour (Palette::noise);
            drawDashedHorizontal (g, y, w);
        }

        if (! sounding)
            return;

        g.setColour (Palette::bright);
        g.drawVerticalLine ((int) (centroid * w), 0.0f, h);

        if (pitch > 0.0f)
        {
            const float x = SpectrumData::positionForHz (pitch) * w;
            g.setColour (Palette::conf);
            drawDashedVertical (g, x, h);
        }
    }

private:
    static float magToHeight (float v, float h)
    {
        return std::pow (juce::jmax (0.0f, v), 0.55f) * (h - 8.0f);
    }

    static void drawDashedHorizontal (juce::Graphics& g, float y, float w)
    {
        for (float x = 0.0f; x < w; x += 9.0f)
            g.fillRect (x, y, 4.0f, 1.0f);
    }

    static void drawDashedVertical (juce::Graphics& g, float x, float h)
    {
        for (float y = 0.0f; y < h; y += 13.0f)
            g.fillRect (x, y, 1.0f, 7.0f);
    }

    const SpectrumData& data;
    float centroid = 0.0f;
    float pitch = 0.0f;
    bool sounding = false;
};

// ---------------------------------------------------------------------
// A meter carrying its own thresholds as draggable markers. The whole
// point: "why isn't it triggering" becomes visible rather than deduced.
// ---------------------------------------------------------------------
class ThresholdMeter : public juce::Component
{
public:
    ThresholdMeter (juce::String meterName, juce::Colour barColour)
        : name (std::move (meterName)), colour (barColour) {}

    // Markers sit at the parameter's real value on the meter's own 0-1
    // axis, not at its normalised position, so the marker lines up with
    // the level it actually gates.
    void addMarker (juce::RangedAudioParameter* param, juce::String tip, bool primary)
    {
        markers.push_back ({ param, std::move (tip), primary });
    }

    void setValue (float v) { value = juce::jlimit (0.0f, 1.0f, v); }
    void setBarColour (juce::Colour c) { colour = c; }

    float getPrimaryThreshold() const
    {
        for (auto& m : markers)
            if (m.primary && m.param != nullptr)
                return m.param->convertFrom0to1 (m.param->getValue());
        return 0.0f;
    }

    void paint (juce::Graphics& g) override
    {
        auto full = getLocalBounds();
        auto head = full.removeFromTop (14);

        drawLabel (g, name, head, Palette::muted, 10.0f);
        g.setColour (juce::Colour (0xff98a0b0));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (value, 2), head, juce::Justification::centredRight);

        full.removeFromTop (4);
        trackArea = full.removeFromTop (22);

        auto tr = trackArea.toFloat();
        g.setColour (Palette::well);
        g.fillRoundedRectangle (tr, 3.0f);
        g.setColour (juce::Colour (0xff23262e));
        g.drawRoundedRectangle (tr.reduced (0.5f), 3.0f, 1.0f);

        // The bar brightens once it crosses its threshold, so it shows
        // whether the plugin will act, not merely what the level is.
        const bool hot = value >= getPrimaryThreshold();
        g.setColour (colour.withAlpha (hot ? 1.0f : 0.5f));
        g.fillRoundedRectangle (tr.withWidth (juce::jmax (2.0f, tr.getWidth() * value)), 3.0f);

        for (auto& m : markers)
        {
            if (m.param == nullptr)
                continue;

            const float pos = juce::jlimit (0.0f, 1.0f, m.param->convertFrom0to1 (m.param->getValue()));
            const float x = tr.getX() + pos * tr.getWidth();
            const auto c = m.primary ? Palette::bone : juce::Colour (0xff7f8492);

            g.setColour (c);
            g.fillRect (x - 0.5f, tr.getY() - 3.0f, 1.0f, tr.getHeight() + 6.0f);
            g.fillRoundedRectangle (x - 4.5f, tr.getY() - 4.0f, 9.0f, 5.0f, 1.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { dragMarker (e); }
    void mouseDrag (const juce::MouseEvent& e) override { dragMarker (e); }

    static constexpr int preferredHeight = 40;

private:
    struct Marker
    {
        juce::RangedAudioParameter* param;
        juce::String tip;
        bool primary;
    };

    void dragMarker (const juce::MouseEvent& e)
    {
        if (markers.empty() || trackArea.getWidth() <= 0)
            return;

        const float pos = juce::jlimit (0.0f, 1.0f,
            (float) (e.position.x - trackArea.getX()) / (float) trackArea.getWidth());

        // Grab whichever marker is nearest, so two markers can share one
        // track without needing separate hit zones.
        Marker* nearest = nullptr;
        float best = 1.0e9f;

        for (auto& m : markers)
        {
            if (m.param == nullptr)
                continue;
            const float d = std::abs (m.param->convertFrom0to1 (m.param->getValue()) - pos);
            if (d < best) { best = d; nearest = &m; }
        }

        if (nearest != nullptr)
        {
            nearest->param->beginChangeGesture();
            nearest->param->setValueNotifyingHost (nearest->param->convertTo0to1 (pos));
            nearest->param->endChangeGesture();
            repaint();
        }
    }

    juce::String name;
    juce::Colour colour;
    float value = 0.0f;
    std::vector<Marker> markers;
    juce::Rectangle<int> trackArea;
};

// ---------------------------------------------------------------------
// Waveform scope, running the same morph maths the oscillator uses.
// ---------------------------------------------------------------------
class WaveScope : public juce::Component
{
public:
    void setPositions (float baseMorph, float liveMorph)
    {
        base = baseMorph;
        live = liveMorph;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Palette::well);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colour (0xff23262e));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);

        g.setColour (juce::Colour (0xff1e222a));
        g.drawHorizontalLine ((int) r.getCentreY(), 0.0f, r.getWidth());

        drawTrace (g, base, juce::Colour (0xff4d5361), false);
        drawTrace (g, live, Palette::bone, true);
    }

private:
    void drawTrace (juce::Graphics& g, float morph, juce::Colour colour, bool dashed)
    {
        auto r = getLocalBounds().toFloat();
        const float amp = (r.getHeight() * 0.5f - 8.0f) * 0.85f;

        juce::Path path;
        for (int x = 0; x <= (int) r.getWidth(); ++x)
        {
            const float phase = std::fmod ((float) x / r.getWidth() * 2.0f, 1.0f);
            const float y = r.getCentreY() - shapeAt (phase, morph) * amp;
            x == 0 ? path.startNewSubPath ((float) x, y) : path.lineTo ((float) x, y);
        }

        g.setColour (colour);
        if (dashed)
        {
            const float dashes[] = { 4.0f, 4.0f };
            juce::Path stroked;
            juce::PathStrokeType (1.6f).createDashedStroke (stroked, path, dashes, 2);
            g.fillPath (stroked);
        }
        else
        {
            g.strokePath (path, juce::PathStrokeType (1.6f));
        }
    }

    // Mirrors MorphOscillator's shaping, minus the band-limiting, which
    // only matters for audio.
    static float shapeAt (float p, float morph)
    {
        const float tri = 4.0f * std::abs (p - 0.5f) - 1.0f;
        const float saw = 2.0f * p - 1.0f;

        auto pulse = [p] (float w)
        {
            float q = p + 0.5f;
            if (q >= 1.0f) q -= 1.0f;
            float v = q < w ? 1.0f : -1.0f;
            v -= (2.0f * w - 1.0f);
            v /= (2.0f - 2.0f * w);
            return v * 0.577f;
        };

        if (morph < 1.0f / 3.0f)
        {
            const float k = morph * 3.0f;
            return tri * std::sqrt (1.0f - k) + saw * std::sqrt (k);
        }
        if (morph < 2.0f / 3.0f)
        {
            const float k = (morph - 1.0f / 3.0f) * 3.0f;
            return saw * (1.0f - k) + pulse (0.5f) * k;
        }
        const float k = (morph - 2.0f / 3.0f) * 3.0f;
        return pulse (0.5f - (0.5f - 0.12f) * k);
    }

    float base = 0.25f;
    float live = 0.25f;
};

// ---------------------------------------------------------------------
// Segmented selector for choice and bool parameters.
// ---------------------------------------------------------------------
class SegmentedControl : public juce::Component
{
public:
    std::function<void (int)> onSelect;

    void setOptions (juce::StringArray newOptions)
    {
        options = std::move (newOptions);
        repaint();
    }

    void setSelected (int index)
    {
        selected = index;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Palette::well);
        g.fillRoundedRectangle (r, 4.0f);

        if (options.isEmpty())
            return;

        const float segW = (r.getWidth() - 6.0f) / (float) options.size();

        for (int i = 0; i < options.size(); ++i)
        {
            auto seg = juce::Rectangle<float> (3.0f + i * segW, 3.0f,
                                                segW, r.getHeight() - 6.0f);
            if (i == selected)
            {
                g.setColour (Palette::panel2);
                g.fillRoundedRectangle (seg, 3.0f);
            }

            g.setColour (i == selected ? Palette::bone : Palette::muted);
            g.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
            g.drawText (options[i].toUpperCase(), seg.toNearestInt(),
                         juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (options.isEmpty())
            return;

        const int index = juce::jlimit (0, options.size() - 1,
            (int) ((e.position.x - 3.0f) / ((getWidth() - 6.0f) / (float) options.size())));

        selected = index;
        if (onSelect) onSelect (index);
        repaint();
    }

private:
    juce::StringArray options;
    int selected = 0;
};
