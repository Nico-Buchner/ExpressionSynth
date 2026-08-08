#include "Arpeggiator.h"
#include <algorithm>
#include "ParameterFormat.h"

void Arpeggiator::addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params)
{
    using Choice = juce::AudioParameterChoice;
    using Float = juce::AudioParameterFloat;

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { enabledParamID, 1 }, "Arp", false));

    params.push_back (std::make_unique<Choice> (
        juce::ParameterID { sourceParamID, 1 }, "Arp Source", getSourceNames(), 2));

    params.push_back (std::make_unique<Choice> (
        juce::ParameterID { patternParamID, 1 }, "Arp Pattern", getPatternNames(), 0));

    params.push_back (std::make_unique<Choice> (
        juce::ParameterID { rateParamID, 1 }, "Arp Rate", getRateNames(), 3));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { octavesParamID, 1 }, "Arp Octaves", 1, maxOctaves, 1,
        ParameterFormat::intFmt (ParameterFormat::octaveCount)));

    params.push_back (std::make_unique<Float> (
        juce::ParameterID { gateParamID, 1 }, "Arp Gate",
        juce::NormalisableRange<float> (0.05f, 1.0f), 0.5f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));

    params.push_back (std::make_unique<Float> (
        juce::ParameterID { swingParamID, 1 }, "Arp Swing",
        juce::NormalisableRange<float> (0.0f, 0.75f), 0.0f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));

    params.push_back (std::make_unique<Choice> (
        juce::ParameterID { chordParamID, 1 }, "Arp Chord", getChordNames(), 0));

    params.push_back (std::make_unique<Float> (
        juce::ParameterID { freeBpmParamID, 1 }, "Arp Free Tempo",
        juce::NormalisableRange<float> (40.0f, 240.0f, 1.0f), 120.0f,
        ParameterFormat::floatFmt (ParameterFormat::bpm)));
}

void Arpeggiator::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    latchClearSamples = (int) (sampleRate * 2.0);
    reset();
}

void Arpeggiator::reset()
{
    pool.clear();
    pendingOffs.clear();
    stepCounter = 0;
    samplesToNextStep = 0.0;
    lastDetectedNote = -1;
    silenceSamples = 0;
}

void Arpeggiator::setParams (juce::AudioProcessorValueTreeState& params)
{
    const bool wasEnabled = enabled;
    enabled = params.getRawParameterValue (enabledParamID)->load() > 0.5f;

    const auto newSource = (Source) (int) params.getRawParameterValue (sourceParamID)->load();
    if (newSource != source || (enabled && ! wasEnabled))
    {
        pool.clear();
        stepCounter = 0;
        samplesToNextStep = 0.0;
    }
    source = newSource;

    pattern    = (Pattern) (int) params.getRawParameterValue (patternParamID)->load();
    rateIndex  = (int) params.getRawParameterValue (rateParamID)->load();
    octaves    = juce::jlimit (1, maxOctaves, (int) params.getRawParameterValue (octavesParamID)->load());
    gate       = params.getRawParameterValue (gateParamID)->load();
    swing      = params.getRawParameterValue (swingParamID)->load();
    chordIndex = (int) params.getRawParameterValue (chordParamID)->load();
    freeBpm    = params.getRawParameterValue (freeBpmParamID)->load();
}

double Arpeggiator::stepLengthSamples (double bpm) const
{
    // Multiples of a quarter note.
    static const double multiplier[] = { 1.0, 0.5, 1.0/3.0, 0.25, 1.0/6.0, 0.125 };
    const double idx = juce::jlimit (0, 5, rateIndex);
    const double quarter = (60.0 / juce::jmax (20.0, bpm)) * sampleRate;
    const double base = quarter * multiplier[(int) idx];

    // Swing lengthens even steps and shortens odd ones by the same
    // amount, so a pair still occupies the time two straight steps would.
    return (stepCounter % 2 == 0)
        ? base * (1.0 + (double) swing * 0.5)
        : base * (1.0 - (double) swing * 0.5);
}

void Arpeggiator::addToPool (int note, int channel, bool allowDuplicates)
{
    if (! allowDuplicates)
        for (const auto& p : pool)
            if (p.note == note)
                return;

    if ((int) pool.size() >= maxPool)
        pool.erase (pool.begin());

    pool.push_back ({ note, channel });
}

void Arpeggiator::rebuildGenerated (int rootNote)
{
    static const std::vector<std::vector<int>> chords =
    {
        { 0, 4, 7 },        // Major
        { 0, 3, 7 },        // Minor
        { 0, 4, 7, 11 },    // Major 7
        { 0, 3, 7, 10 },    // Minor 7
        { 0, 5, 7 },        // Sus 4
        { 0, 7 },           // Fifth
        { 0, 12 }           // Octave
    };

    pool.clear();
    const auto& intervals = chords[(size_t) juce::jlimit (0, 6, chordIndex)];

    for (int semitones : intervals)
        pool.push_back ({ juce::jlimit (0, 127, rootNote + semitones),
                           NoteOrigin::audioChannel });
}

void Arpeggiator::collectNotes (const juce::MidiBuffer& input, int detectedNote)
{
    switch (source)
    {
        case Source::MidiHeld:
            // Conventional behaviour: the pool is whatever is held down.
            for (const auto meta : input)
            {
                const auto m = meta.getMessage();
                if (m.getChannel() == NoteOrigin::audioChannel)
                    continue;

                if (m.isNoteOn())
                    addToPool (m.getNoteNumber(), m.getChannel(), false);
                else if (m.isNoteOff())
                    pool.erase (std::remove_if (pool.begin(), pool.end(),
                        [&m] (const PoolNote& p) { return p.note == m.getNoteNumber(); }),
                        pool.end());
            }
            break;

        case Source::Generated:
            if (detectedNote >= 0 && detectedNote != lastDetectedNote)
                rebuildGenerated (detectedNote);
            else if (detectedNote < 0)
                pool.clear();
            break;

        case Source::LatchedAudio:
            for (const auto meta : input)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn() && m.getChannel() == NoteOrigin::audioChannel)
                    addToPool (m.getNoteNumber(), NoteOrigin::audioChannel, false);
            }
            break;
    }

    lastDetectedNote = detectedNote;
}

int Arpeggiator::indexForStep (int step, int poolSize) const
{
    if (poolSize <= 0)
        return 0;

    switch (pattern)
    {
        case Pattern::Up:       return step % poolSize;
        case Pattern::Down:     return poolSize - 1 - (step % poolSize);
        case Pattern::AsPlayed: return step % poolSize;
        case Pattern::Random:   return juce::Random::getSystemRandom().nextInt (poolSize);

        case Pattern::UpDown:
        {
            // Endpoints are not repeated, so a two-note pool alternates
            // rather than stuttering.
            const int span = juce::jmax (1, poolSize * 2 - 2);
            const int p = step % span;
            return p < poolSize ? p : span - p;
        }

        case Pattern::DownUp:
        {
            const int span = juce::jmax (1, poolSize * 2 - 2);
            const int p = step % span;
            const int up = p < poolSize ? p : span - p;
            return poolSize - 1 - up;
        }
    }
    return 0;
}

void Arpeggiator::process (const juce::MidiBuffer& input, int detectedNote,
                            juce::MidiBuffer& output, int numSamples, double bpm)
{
    if (! enabled)
    {
        output.addEvents (input, 0, numSamples, 0);
        return;
    }

    collectNotes (input, detectedNote);

    // A latched pool with nothing arriving would hold the first thing
    // played indefinitely.
    if (source == Source::LatchedAudio)
    {
        if (detectedNote >= 0)
            silenceSamples = 0;
        else if ((silenceSamples += numSamples) > latchClearSamples)
            pool.clear();
    }

    const int poolSize = (int) pool.size();
    const int effective = poolSize * octaves;

    for (int i = 0; i < numSamples; ++i)
    {
        for (auto it = pendingOffs.begin(); it != pendingOffs.end(); )
        {
            if (--it->samplesRemaining <= 0)
            {
                output.addEvent (juce::MidiMessage::noteOff (it->channel, it->note), i);
                it = pendingOffs.erase (it);
            }
            else
            {
                ++it;
            }
        }

        samplesToNextStep -= 1.0;

        if (samplesToNextStep <= 0.0)
        {
            const double length = stepLengthSamples (bpm);
            samplesToNextStep += length;

            if (effective > 0)
            {
                const int index = indexForStep (stepCounter, effective);
                const int octave = index / juce::jmax (1, poolSize);
                const auto& src = pool[(size_t) juce::jlimit (0, poolSize - 1,
                                                                index % juce::jmax (1, poolSize))];
                const int note = juce::jlimit (0, 127, src.note + octave * 12);

                output.addEvent (juce::MidiMessage::noteOn (src.channel, note, (juce::uint8) 100), i);
                pendingOffs.push_back ({ note, src.channel,
                                          juce::jmax (1, (int) (length * (double) gate)) });
            }

            ++stepCounter;
        }
    }
}
