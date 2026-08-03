#pragma once
#include <juce_dsp/juce_dsp.h>

/**
    Analyzes an incoming audio buffer and extracts expression-relevant
    features on a per-block basis. Keep this class purely analytical —
    it should never touch synth parameters directly. ExpressionMapper
    owns that responsibility.
*/
class FeatureExtractor
{
public:
    struct Features
    {
        float amplitude      = 0.0f;   // smoothed RMS, 0-1
        float pitchHz        = 0.0f;   // 0 = unvoiced/no pitch detected
        float pitchConfidence = 0.0f;  // 0-1, how reliable pitchHz is this block
        float spectralCentroid = 0.0f; // normalized 0-1 (brightness)
        float spectralFlatness = 0.0f; // 0-1, noisiness/breathiness
        float onsetStrength  = 0.0f;   // 0-1, transient energy this block
    };

    void prepare (double sampleRate, int blockSize);
    void reset();

    // Envelope response is articulation-dependent (a mallet strike and a
    // bow swell need very different attack times), so profiles drive
    // this at runtime rather than it being fixed in prepare().
    void setEnvelopeTimes (float attackMs, float releaseMs);

    // Call once per processBlock with the input audio (mono-summed upstream).
    void process (const juce::AudioBuffer<float>& input);

    const Features& getLatestFeatures() const noexcept { return current; }

private:
    void updateAmplitude (const juce::AudioBuffer<float>& input);
    void updatePitch (const juce::AudioBuffer<float>& input);
    void updateSpectralFeatures (const juce::AudioBuffer<float>& input);
    void updateOnset (float currentAmplitude);

    double sampleRate = 44100.0;
    int blockSize = 512;

    Features current;

    // Envelope follower state
    float ampEnvelope = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;

    // Onset detection state
    float previousAmplitude = 0.0f;

    // Pitch tracker (YIN) scratch buffers
    juce::AudioBuffer<float> pitchScratch;
    static constexpr int pitchBufferSize = 2048; // ~46ms @ 44.1k, adjust for lowest note

    std::vector<float> yinDiff;   // difference function, size pitchBufferSize/2
    std::vector<float> yinCmnd;   // cumulative mean normalized difference, same size
    static constexpr float yinThreshold = 0.15f; // lower = stricter/more confident-only detections
    float lastConfidentPitchHz = 0.0f;

    // FFT for spectral centroid / flatness. Reuses pitchScratch as its
    // input window (both are 2048 samples) rather than keeping a second
    // rolling buffer — see static_assert in the .cpp.
    std::unique_ptr<juce::dsp::FFT> fft;
    juce::AudioBuffer<float> fftBuffer;
    juce::HeapBlock<float> window;
    static constexpr int fftOrder = 11; // 2048-point FFT
};
