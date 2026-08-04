#include "FeatureExtractor.h"

void FeatureExtractor::prepare (double newSampleRate, int newBlockSize)
{
    sampleRate = newSampleRate;
    blockSize = newBlockSize;

    setEnvelopeTimes (5.0f, 80.0f);
    setPitchRange (75.0f, 2000.0f);

    pitchScratch.setSize (1, pitchBufferSize);
    yinDiff.assign (pitchBufferSize / 2, 0.0f);
    yinCmnd.assign (pitchBufferSize / 2, 0.0f);

    fftBuffer.setSize (1, (1 << (fftOrder + 1)));
    window.malloc (1 << fftOrder);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (
        window.getData(), 1 << fftOrder, juce::dsp::WindowingFunction<float>::hann);
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    reset();
}

void FeatureExtractor::setEnvelopeTimes (float attackMs, float releaseMs)
{
    attackMs  = juce::jmax (0.1f, attackMs);
    releaseMs = juce::jmax (0.1f, releaseMs);
    attackCoeff  = std::exp (-1.0f / (float) (attackMs  * 0.001 * sampleRate));
    releaseCoeff = std::exp (-1.0f / (float) (releaseMs * 0.001 * sampleRate));
}

void FeatureExtractor::setPitchRange (float minHz, float maxHz)
{
    const int hardLimit = pitchBufferSize / 2;
    minHz = juce::jmax (20.0f, minHz);
    maxHz = juce::jmax (minHz + 1.0f, maxHz);

    tauMin = juce::jlimit (2, hardLimit - 1, (int) (sampleRate / maxHz));
    tauMax = juce::jlimit (tauMin + 1, hardLimit, (int) (sampleRate / minHz) + 1);
}

void FeatureExtractor::reset()
{
    current = {};
    ampEnvelope = 0.0f;
    previousAmplitude = 0.0f;
    pitchScratch.clear();
    lastConfidentPitchHz = 0.0f;
}

void FeatureExtractor::process (const juce::AudioBuffer<float>& input)
{
    updateAmplitude (input);
    updatePitch (input);
    updateSpectralFeatures (input);
    updateOnset (current.amplitude);
}

void FeatureExtractor::updateAmplitude (const juce::AudioBuffer<float>& input)
{
    auto* data = input.getReadPointer (0);
    float peak = 0.0f;

    for (int i = 0; i < input.getNumSamples(); ++i)
    {
        const float rectified = std::abs (data[i]);
        const float coeff = rectified > ampEnvelope ? attackCoeff : releaseCoeff;
        ampEnvelope = coeff * ampEnvelope + (1.0f - coeff) * rectified;
        peak = juce::jmax (peak, ampEnvelope);
    }

    current.amplitude = juce::jlimit (0.0f, 1.0f, peak);
}

void FeatureExtractor::updatePitch (const juce::AudioBuffer<float>& input)
{
    const int numNew = input.getNumSamples();
    auto* scratch = pitchScratch.getWritePointer (0);
    auto* in = input.getReadPointer (0);

    if (numNew >= pitchBufferSize)
    {
        std::copy (in + (numNew - pitchBufferSize), in + numNew, scratch);
    }
    else
    {
        std::memmove (scratch, scratch + numNew, sizeof (float) * (size_t) (pitchBufferSize - numNew));
        std::copy (in, in + numNew, scratch + (pitchBufferSize - numNew));
    }

    const int windowLen = pitchBufferSize / 2;

    // Step 1: difference function, only over the tau range implied by
    // the configured min/max frequency.
    for (int tau = 1; tau < tauMax; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < windowLen; ++j)
        {
            const float delta = scratch[j] - scratch[j + tau];
            sum += delta * delta;
        }
        yinDiff[(size_t) tau] = sum;
    }

    // Step 2: cumulative mean normalized difference. Must accumulate
    // from tau 1 even below tauMin, or the normalisation is wrong.
    yinCmnd[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau < tauMax; ++tau)
    {
        runningSum += yinDiff[(size_t) tau];
        yinCmnd[(size_t) tau] = runningSum > 0.0f
            ? yinDiff[(size_t) tau] * (float) tau / runningSum
            : 1.0f;
    }

    // Step 3: absolute threshold, searched only within range.
    int tauEstimate = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (yinCmnd[(size_t) tau] < yinThreshold)
        {
            while (tau + 1 < tauMax && yinCmnd[(size_t) (tau + 1)] < yinCmnd[(size_t) tau])
                ++tau;
            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate == -1)
    {
        current.pitchConfidence = juce::jmax (0.0f, current.pitchConfidence - 0.1f);
        current.pitchHz = lastConfidentPitchHz;
        return;
    }

    // Step 4: parabolic interpolation for sub-sample precision.
    float betterTau = (float) tauEstimate;
    if (tauEstimate > tauMin && tauEstimate < tauMax - 1)
    {
        const float s0 = yinCmnd[(size_t) (tauEstimate - 1)];
        const float s1 = yinCmnd[(size_t) tauEstimate];
        const float s2 = yinCmnd[(size_t) (tauEstimate + 1)];
        const float denom = s0 - 2.0f * s1 + s2;
        if (std::abs (denom) > 1.0e-9f)
            betterTau += 0.5f * (s0 - s2) / denom;
    }

    if (betterTau > 0.0f)
    {
        current.pitchHz = (float) sampleRate / betterTau;
        current.pitchConfidence = juce::jlimit (0.0f, 1.0f, 1.0f - yinCmnd[(size_t) tauEstimate]);
        lastConfidentPitchHz = current.pitchHz;
    }
}

void FeatureExtractor::updateSpectralFeatures (const juce::AudioBuffer<float>&)
{
    static_assert (pitchBufferSize == (1 << fftOrder),
                    "FFT window and pitch buffer sizes are assumed equal");

    constexpr int fftSize = 1 << fftOrder;
    auto* fftData = fftBuffer.getWritePointer (0);
    auto* source = pitchScratch.getReadPointer (0);

    juce::FloatVectorOperations::clear (fftData, fftBuffer.getNumSamples());
    for (int i = 0; i < fftSize; ++i)
        fftData[i] = source[i] * window[(size_t) i];

    fft->performFrequencyOnlyForwardTransform (fftData);

    const int numBins = fftSize / 2;
    const float binHz = (float) sampleRate / (float) fftSize;

    float weightedFreqSum = 0.0f;
    float magSum = 0.0f;
    float logMagSum = 0.0f;
    int nonZeroBins = 0;

    for (int bin = 1; bin < numBins; ++bin)
    {
        const float mag = fftData[bin];
        weightedFreqSum += (float) bin * binHz * mag;
        magSum += mag;

        if (mag > 1.0e-6f)
        {
            logMagSum += std::log (mag);
            ++nonZeroBins;
        }
    }

    if (magSum > 1.0e-9f)
    {
        const float centroidHz = weightedFreqSum / magSum;
        const float nyquist = (float) sampleRate * 0.5f;
        current.spectralCentroid = juce::jlimit (0.0f, 1.0f, centroidHz / nyquist);
    }

    if (nonZeroBins > 0)
    {
        const float geometricMean = std::exp (logMagSum / (float) nonZeroBins);
        const float arithmeticMean = magSum / (float) numBins;
        current.spectralFlatness = arithmeticMean > 1.0e-9f
            ? juce::jlimit (0.0f, 1.0f, geometricMean / arithmeticMean)
            : 0.0f;
    }
}

void FeatureExtractor::updateOnset (float currentAmplitude)
{
    const float delta = currentAmplitude - previousAmplitude;
    current.onsetStrength = juce::jlimit (0.0f, 1.0f, delta * 4.0f);
    previousAmplitude = currentAmplitude;
}
