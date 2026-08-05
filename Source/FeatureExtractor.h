#pragma once
#include <juce_dsp/juce_dsp.h>
#include "SpectrumData.h"

class FeatureExtractor
{
public:
    struct Features
    {
        float amplitude       = 0.0f;
        float pitchHz         = 0.0f;
        float pitchConfidence = 0.0f;
        float spectralCentroid = 0.0f;
        float spectralFlatness = 0.0f;
        float onsetStrength   = 0.0f;
    };

    void prepare (double sampleRate, int blockSize);
    void reset();

    void setEnvelopeTimes (float attackMs, float releaseMs);

    // Restricting the search to a plausible frequency range does three
    // things: rejects sub-harmonic artifacts on polyphonic input, and
    // roughly halves the cost of the difference function, which is the
    // most expensive thing in the plugin.
    void setPitchRange (float minHz, float maxHz);

    // Analysis span in milliseconds - the dominant term in how long the
    // plugin takes to respond to a note. Shown in the interface so the
    // cost of a wide pitch range is visible rather than hidden.
    float getAnalysisLatencyMs() const noexcept
    {
        return 1000.0f * (float) (windowLen + tauMax) / (float) sampleRate;
    }

    void process (const juce::AudioBuffer<float>& input);

    const Features& getLatestFeatures() const noexcept { return current; }
    const SpectrumData& getSpectrum() const noexcept { return spectrum; }

private:
    void updateAmplitude (const juce::AudioBuffer<float>& input);
    void updatePitch (const juce::AudioBuffer<float>& input);
    void updateSpectralFeatures (const juce::AudioBuffer<float>& input);
    void updateOnset (float currentAmplitude);

    double sampleRate = 44100.0;
    int blockSize = 512;

    Features current;

    float ampEnvelope = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    float previousAmplitude = 0.0f;

    juce::AudioBuffer<float> pitchScratch;
    static constexpr int pitchBufferSize = 2048;

    std::vector<float> yinDiff;
    std::vector<float> yinCmnd;
    static constexpr float yinThreshold = 0.15f;
    float lastConfidentPitchHz = 0.0f;

    int tauMin = 2;
    int tauMax = pitchBufferSize / 2;
    int windowLen = pitchBufferSize / 2;
    int analysisOffset = 0;

    void updateSpectrumDisplay (const float* magnitudes, int numBins);

    SpectrumData spectrum;
    std::vector<int> binToBand;
    std::vector<float> floorTrack;

    std::unique_ptr<juce::dsp::FFT> fft;
    juce::AudioBuffer<float> fftBuffer;
    juce::HeapBlock<float> window;
    static constexpr int fftOrder = 11;
};
