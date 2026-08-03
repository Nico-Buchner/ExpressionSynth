#include "FeatureExtractor.h"

void FeatureExtractor::prepare (double newSampleRate, int newBlockSize)
{
    sampleRate = newSampleRate;
    blockSize = newBlockSize;

    // Defaults; overridden per-articulation via setEnvelopeTimes().
    setEnvelopeTimes (5.0f, 80.0f);

    pitchScratch.setSize (1, pitchBufferSize);
    yinDiff.assign (pitchBufferSize / 2, 0.0f);
    yinCmnd.assign (pitchBufferSize / 2, 0.0f);
    fftBuffer.setSize (1, (1 << (fftOrder + 1))); // real+imag interleaved, JUCE FFT convention
    window.malloc (1 << fftOrder);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (
        window.getData(), 1 << fftOrder, juce::dsp::WindowingFunction<float>::hann);
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    reset();
}

void FeatureExtractor::setEnvelopeTimes (float attackMs, float releaseMs)
{
    // Guard against zero/negative times producing a divide-by-zero in
    // the coefficient calculation.
    attackMs  = juce::jmax (0.1f, attackMs);
    releaseMs = juce::jmax (0.1f, releaseMs);

    attackCoeff  = std::exp (-1.0f / (float) (attackMs  * 0.001 * sampleRate));
    releaseCoeff = std::exp (-1.0f / (float) (releaseMs * 0.001 * sampleRate));
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
    updatePitch (input);          // TODO: implement YIN difference function
    updateSpectralFeatures (input); // TODO: FFT -> centroid + flatness
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
    // Maintain a rolling pitchBufferSize-sample window: shift left by
    // however many new samples arrived this block, append the rest.
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

    const int maxTau = pitchBufferSize / 2;

    // --- Step 1: difference function ---
    // d(tau) = sum_j (x[j] - x[j+tau])^2
    // This is the expensive O(maxTau^2) part — if profiling shows this
    // too costly on iPad, swap for an FFT-based autocorrelation instead
    // (same result, O(n log n)); keeping the direct version first since
    // it's easier to verify correctness against.
    yinDiff[0] = 0.0f;
    for (int tau = 1; tau < maxTau; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < maxTau; ++j)
        {
            const float delta = scratch[j] - scratch[j + tau];
            sum += delta * delta;
        }
        yinDiff[(size_t) tau] = sum;
    }

    // --- Step 2: cumulative mean normalized difference (CMND) ---
    // Normalizes so early lags aren't unfairly favored; this is what
    // separates YIN from plain autocorrelation and is why it handles
    // octave errors much better.
    yinCmnd[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau < maxTau; ++tau)
    {
        runningSum += yinDiff[(size_t) tau];
        yinCmnd[(size_t) tau] = runningSum > 0.0f
            ? yinDiff[(size_t) tau] * (float) tau / runningSum
            : 1.0f;
    }

    // --- Step 3: absolute threshold ---
    // Walk forward until CMND dips below threshold, then continue to
    // the local minimum from there — this avoids locking onto the very
    // first dip if it's noise rather than the true period.
    int tauEstimate = -1;
    for (int tau = 2; tau < maxTau; ++tau)
    {
        if (yinCmnd[(size_t) tau] < yinThreshold)
        {
            while (tau + 1 < maxTau && yinCmnd[(size_t) (tau + 1)] < yinCmnd[(size_t) tau])
                ++tau;
            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate == -1)
    {
        // No period found under threshold — likely unvoiced/noisy/silent.
        // Decay confidence rather than snapping to zero, so a brief
        // dropout mid-note doesn't yank pitch-mapped parameters.
        current.pitchConfidence = juce::jmax (0.0f, current.pitchConfidence - 0.1f);
        current.pitchHz = lastConfidentPitchHz;
        return;
    }

    // --- Step 4: parabolic interpolation for sub-sample tau precision ---
    // Without this, pitch estimates quantize to sampleRate/integer-tau
    // steps, which is audibly steppy on pitch-bend mappings.
    float betterTau = (float) tauEstimate;
    if (tauEstimate > 0 && tauEstimate < maxTau - 1)
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

void FeatureExtractor::updateSpectralFeatures (const juce::AudioBuffer<float>& /*input*/)
{
    // Shares the pitch tracker's rolling window rather than a second
    // buffer — both are sized 2048. updatePitch() must run before this
    // in process() so the window is current (it does).
    static_assert (pitchBufferSize == (1 << fftOrder),
                    "FFT window and pitch buffer sizes are assumed equal");

    constexpr int fftSize = 1 << fftOrder;
    auto* fftData = fftBuffer.getWritePointer (0);
    auto* source = pitchScratch.getReadPointer (0);

    juce::FloatVectorOperations::clear (fftData, fftBuffer.getNumSamples());
    for (int i = 0; i < fftSize; ++i)
        fftData[i] = source[i] * window[(size_t) i];

    // Writes magnitude-only spectrum into the first fftSize/2 bins —
    // simpler than a full complex transform since centroid/flatness
    // only need magnitude, not phase.
    fft->performFrequencyOnlyForwardTransform (fftData);

    const int numBins = fftSize / 2;
    const float binHz = (float) sampleRate / (float) fftSize;

    float weightedFreqSum = 0.0f;
    float magSum = 0.0f;
    float logMagSum = 0.0f;
    int nonZeroBins = 0;

    // Skip bin 0 (DC) — it carries no pitch/timbre information and can
    // dominate the centroid if there's any input offset.
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
        // Flatness = geometric mean / arithmetic mean of the magnitude
        // spectrum. Near 1.0 = noise-like (flat spectrum); near 0.0 =
        // tonal (energy concentrated in few bins).
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
    current.onsetStrength = juce::jlimit (0.0f, 1.0f, delta * 4.0f); // scale factor, tune by ear
    previousAmplitude = currentAmplitude;
}
