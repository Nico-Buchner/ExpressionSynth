#include "FeatureExtractor.h"
#include <algorithm>

void FeatureExtractor::prepare (double newSampleRate, int newBlockSize)
{
    sampleRate = newSampleRate;
    blockSize = newBlockSize;

    setEnvelopeTimes (5.0f, 80.0f);
    setPitchRange (75.0f, 2000.0f);

    // getLatencyMs depends on the range, so it is only meaningful after
    // setPitchRange has run.

    pitchScratch.setSize (1, pitchBufferSize);
    yinDiff.assign (pitchBufferSize / 2, 0.0f);
    yinCmnd.assign (pitchBufferSize / 2, 0.0f);

    fftBuffer.setSize (1, (1 << (fftOrder + 1)));
    window.malloc (1 << fftOrder);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (
        window.getData(), 1 << fftOrder, juce::dsp::WindowingFunction<float>::hann);
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    // Map each FFT bin onto a log-spaced display band once, rather than
    // recomputing logarithms for every bin on every block.
    const int numBins = (1 << fftOrder) / 2;
    const float binHz = (float) sampleRate / (float) (1 << fftOrder);
    binToBand.assign ((size_t) numBins, -1);

    for (int bin = 1; bin < numBins; ++bin)
    {
        const float hz = (float) bin * binHz;
        if (hz < SpectrumData::minHz || hz > SpectrumData::maxHz)
            continue;

        const float pos = SpectrumData::positionForHz (hz);
        binToBand[(size_t) bin] = juce::jlimit (0, SpectrumData::numBands - 1,
            (int) std::round (pos * (SpectrumData::numBands - 1)));
    }

    floorTrack.assign (SpectrumData::numBands, 1.0f);
    spectrum.reset();

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

    // The reference frame is sized to the longest lag rather than fixed,
    // which is YIN's conventional configuration and gives an analysis
    // span of exactly two periods of the lowest detectable note - the
    // theoretical minimum. A fixed frame is oversized for every range
    // narrower than the widest, and the excess is latency for nothing.
    windowLen = juce::jlimit (32, pitchBufferSize - tauMax, tauMax);

    // Analyse the NEWEST samples. The buffer holds 2048 with the most
    // recent at the end, so reading from index 0 would analyse audio that
    // is already old and add latency with no benefit.
    analysisOffset = juce::jmax (0, pitchBufferSize - (windowLen + tauMax));
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

    const float* frame = scratch + analysisOffset;

    // Step 1: difference function, over the lag range implied by the
    // configured frequency range, on the most recent audio.
    for (int tau = 1; tau < tauMax; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < windowLen; ++j)
        {
            const float delta = frame[j] - frame[j + tau];
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

    updateSpectrumDisplay (fftData, numBins);
}

void FeatureExtractor::updateSpectrumDisplay (const float* magnitudes, int numBins)
{
    float banded[SpectrumData::numBands] = {};

    // Highest magnitude wins the band. Averaging would smear narrow
    // partials into the noise around them, which is exactly the detail
    // the display exists to show.
    for (int bin = 1; bin < numBins; ++bin)
    {
        const int band = binToBand[(size_t) bin];
        if (band >= 0)
            banded[band] = juce::jmax (banded[band], magnitudes[bin]);
    }

    // Normalise against the loudest band so the display is readable at
    // any input level; absolute magnitudes are not meaningful here.
    float loudest = 0.0f;
    for (float v : banded)
        loudest = juce::jmax (loudest, v);

    const float norm = loudest > 1.0e-6f ? 1.0f / loudest : 0.0f;

    for (int b = 0; b < SpectrumData::numBands; ++b)
    {
        const float value = banded[b] * norm;

        spectrum.bands[b].store (value, std::memory_order_relaxed);

        // Peak hold falls slowly, so a transient partial stays readable.
        const float prevPeak = spectrum.peaks[b].load (std::memory_order_relaxed);
        spectrum.peaks[b].store (juce::jmax (prevPeak * 0.96f, value),
                                  std::memory_order_relaxed);

        // Minimum tracker: falls fast, rises very slowly, so a loud note
        // passing through cannot drag the floor estimate up with it.
        floorTrack[(size_t) b] = value < floorTrack[(size_t) b]
            ? value
            : floorTrack[(size_t) b] * 0.9995f + value * 0.0005f;
    }

    // Median of the per-band minima. Partials sitting near this level are
    // the ones the pitch tracker will struggle to resolve.
    std::vector<float> sorted (floorTrack);
    std::nth_element (sorted.begin(),
                       sorted.begin() + sorted.size() / 2,
                       sorted.end());
    spectrum.noiseFloor.store (sorted[sorted.size() / 2], std::memory_order_relaxed);
}

void FeatureExtractor::updateOnset (float currentAmplitude)
{
    const float delta = currentAmplitude - previousAmplitude;
    current.onsetStrength = juce::jlimit (0.0f, 1.0f, delta * 4.0f);
    previousAmplitude = currentAmplitude;
}
