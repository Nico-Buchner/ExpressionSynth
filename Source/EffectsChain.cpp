#include "EffectsChain.h"
#include "ParameterFormat.h"

void EffectsChain::addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params)
{
    using Float = juce::AudioParameterFloat;
    using Range = juce::NormalisableRange<float>;

    params.push_back (std::make_unique<Float> (
        juce::ParameterID { driveParamID, 1 }, "Drive", Range (0.0f, 1.0f), 0.0f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));
    params.push_back (std::make_unique<Float> (
        juce::ParameterID { driveToneParamID, 1 }, "Drive Tone", Range (0.0f, 1.0f), 0.5f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));
    params.push_back (std::make_unique<Float> (
        juce::ParameterID { driveLevelParamID, 1 }, "Drive Level", Range (0.0f, 1.0f), 0.8f,
        ParameterFormat::floatFmt (ParameterFormat::decibels)));
    params.push_back (std::make_unique<Float> (
        juce::ParameterID { driveMixParamID, 1 }, "Drive Mix", Range (0.0f, 1.0f), 1.0f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));

    params.push_back (std::make_unique<Float> (
        juce::ParameterID { delayTimeParamID, 1 }, "Delay Time (ms)",
        Range (10.0f, 2000.0f, 1.0f, 0.4f), 375.0f,
        ParameterFormat::floatFmt (ParameterFormat::milliseconds)));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { delaySyncParamID, 1 }, "Delay Sync", true));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { delayDivisionParamID, 1 }, "Delay Division",
        getDivisionNames(), 1));
    params.push_back (std::make_unique<Float> (
        juce::ParameterID { delayFeedbackParamID, 1 }, "Delay Feedback",
        Range (0.0f, 0.95f), 0.35f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));
    params.push_back (std::make_unique<Float> (
        juce::ParameterID { delayDampParamID, 1 }, "Delay Damping", Range (0.0f, 1.0f), 0.4f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));
    params.push_back (std::make_unique<Float> (
        juce::ParameterID { delayMixParamID, 1 }, "Delay Mix", Range (0.0f, 1.0f), 0.0f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));

    params.push_back (std::make_unique<Float> (
        juce::ParameterID { reverbSizeParamID, 1 }, "Reverb Size", Range (0.0f, 1.0f), 0.5f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));
    params.push_back (std::make_unique<Float> (
        juce::ParameterID { reverbDampParamID, 1 }, "Reverb Damping", Range (0.0f, 1.0f), 0.5f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));
    params.push_back (std::make_unique<Float> (
        juce::ParameterID { reverbMixParamID, 1 }, "Reverb Mix", Range (0.0f, 1.0f), 0.0f,
        ParameterFormat::floatFmt (ParameterFormat::percent)));
}

void EffectsChain::prepare (double newSampleRate, int blockSize, int channels)
{
    sampleRate = newSampleRate;
    numChannels = juce::jlimit (1, 2, channels);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, blockSize);
    spec.numChannels = (juce::uint32) numChannels;

    delayLine.prepare (spec);
    delayLine.setMaximumDelayInSamples ((int) (sampleRate * 2.1));

    reverb.setSampleRate (sampleRate);

    for (auto* s : { &driveAmount, &driveMix, &driveLevel,
                      &delayMix, &delayFeedback, &reverbMix })
        s->reset (sampleRate, 0.03);

    // Delay time is smoothed more slowly: a fast change sweeps the read
    // position and pitch-shifts the tail, which is only wanted if asked
    // for.
    delaySamples.reset (sampleRate, 0.20);

    reset();
}

void EffectsChain::reset()
{
    delayLine.reset();
    reverb.reset();
    toneState[0] = toneState[1] = 0.0f;
    dampState[0] = dampState[1] = 0.0f;
}

void EffectsChain::process (juce::AudioBuffer<float>& buffer,
                             juce::AudioProcessorValueTreeState& params,
                             const ModulationState& modulation,
                             double bpm)
{
    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin (numChannels, buffer.getNumChannels());
    if (channels <= 0)
        return;

    // --- targets, base parameter plus whatever the matrix is adding ---
    const float amount = juce::jlimit (0.0f, 1.0f,
        params.getRawParameterValue (driveParamID)->load() + modulation.driveAmount.load());
    driveAmount.setTargetValue (amount);
    driveMix.setTargetValue (params.getRawParameterValue (driveMixParamID)->load());
    driveLevel.setTargetValue (params.getRawParameterValue (driveLevelParamID)->load());

    delayMix.setTargetValue (juce::jlimit (0.0f, 1.0f,
        params.getRawParameterValue (delayMixParamID)->load() + modulation.delayMix.load()));

    // Feedback is clamped hard: a modulation source pushing it past unity
    // would build without bound.
    delayFeedback.setTargetValue (juce::jlimit (0.0f, 0.95f,
        params.getRawParameterValue (delayFeedbackParamID)->load()
        + modulation.delayFeedback.load() * 0.5f));

    reverbMix.setTargetValue (juce::jlimit (0.0f, 1.0f,
        params.getRawParameterValue (reverbMixParamID)->load() + modulation.reverbMix.load()));

    // --- delay time ---
    float timeMs = params.getRawParameterValue (delayTimeParamID)->load();

    if (params.getRawParameterValue (delaySyncParamID)->load() > 0.5f)
    {
        static const double mult[] = { 1.0, 0.5, 1.0/3.0, 0.75, 0.25, 1.0/6.0 };
        const int idx = juce::jlimit (0, 5,
            (int) params.getRawParameterValue (delayDivisionParamID)->load());
        timeMs = (float) ((60000.0 / juce::jmax (20.0, bpm)) * mult[idx]);
    }

    delaySamples.setTargetValue (juce::jlimit (1.0f, (float) (sampleRate * 2.0),
                                                timeMs * 0.001f * (float) sampleRate));

    const float tone = params.getRawParameterValue (driveToneParamID)->load();
    const float damp = params.getRawParameterValue (delayDampParamID)->load();

    // One-pole coefficients. Tone tilts the drive's output; damping
    // darkens each delay repeat, which is what stops a long feedback
    // becoming harsh.
    const float toneCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi
                                        * (200.0f + tone * 9000.0f) / (float) sampleRate);
    const float dampCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi
                                        * (700.0f + (1.0f - damp) * 12000.0f) / (float) sampleRate);

    // --- drive ---
    for (int i = 0; i < numSamples; ++i)
    {
        const float a = driveAmount.getNextValue();
        const float g = driveGain (a);
        const float mk = makeupFor (a, g);
        const float mix = driveMix.getNextValue();
        const float lvl = driveLevel.getNextValue();

        for (int ch = 0; ch < channels; ++ch)
        {
            const float dry = buffer.getSample (ch, i);
            float wet = shape (dry, a, g) * mk;

            toneState[ch] = toneCoeff * toneState[ch] + (1.0f - toneCoeff) * wet;
            wet = juce::jmap (tone, toneState[ch], wet);

            buffer.setSample (ch, i, juce::jmap (mix, dry, wet * lvl));
        }
    }

    // --- delay ---
    for (int i = 0; i < numSamples; ++i)
    {
        const float d = delaySamples.getNextValue();
        const float fb = delayFeedback.getNextValue();
        const float mix = delayMix.getNextValue();

        for (int ch = 0; ch < channels; ++ch)
        {
            const float dry = buffer.getSample (ch, i);

            delayLine.setDelay (d);
            float wet = delayLine.popSample (ch);

            dampState[ch] = dampCoeff * dampState[ch] + (1.0f - dampCoeff) * wet;
            delayLine.pushSample (ch, dry + dampState[ch] * fb);

            buffer.setSample (ch, i, dry + wet * mix);
        }
    }

    // --- reverb ---
    const float verbMixNow = reverbMix.getTargetValue();

    if (verbMixNow > 0.001f)
    {
        reverbParams.roomSize = params.getRawParameterValue (reverbSizeParamID)->load();
        reverbParams.damping  = params.getRawParameterValue (reverbDampParamID)->load();
        reverbParams.wetLevel = verbMixNow;
        reverbParams.dryLevel = 1.0f - verbMixNow * 0.4f;
        reverbParams.width    = 1.0f;
        reverb.setParameters (reverbParams);

        if (channels >= 2)
            reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
        else
            reverb.processMono (buffer.getWritePointer (0), numSamples);
    }
}
