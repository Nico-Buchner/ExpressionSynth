#pragma once
#include <atomic>
#include <cmath>

// Log-spaced band summary of the analysis FFT, for display only.
//
// The FFT already runs every block to derive spectral centroid and
// flatness, so filling this costs almost nothing beyond the copy.
//
// Written on the audio thread, read on the message thread. The atomics
// are relaxed and there is deliberately no lock: a torn read here is one
// slightly wrong bar for one frame, which is invisible, and a lock on
// the audio thread would not be.
struct SpectrumData
{
    static constexpr int numBands = 56;
    static constexpr float minHz = 50.0f;
    static constexpr float maxHz = 8000.0f;

    std::atomic<float> bands[numBands];
    std::atomic<float> peaks[numBands];
    std::atomic<float> noiseFloor { 0.0f };

    // Centre frequency of each band, filled once at prepare time.
    float bandHz[numBands] {};

    SpectrumData()
    {
        for (int i = 0; i < numBands; ++i)
        {
            bands[i].store (0.0f, std::memory_order_relaxed);
            peaks[i].store (0.0f, std::memory_order_relaxed);
            bandHz[i] = bandCentre (i);
        }
    }

    static float bandCentre (int index)
    {
        const float t = (float) index / (float) (numBands - 1);
        return minHz * std::pow (maxHz / minHz, t);
    }

    // Fractional band position for a frequency, for placing markers.
    static float positionForHz (float hz)
    {
        if (hz <= minHz) return 0.0f;
        if (hz >= maxHz) return 1.0f;
        return std::log (hz / minHz) / std::log (maxHz / minHz);
    }

    void reset()
    {
        for (int i = 0; i < numBands; ++i)
        {
            bands[i].store (0.0f, std::memory_order_relaxed);
            peaks[i].store (0.0f, std::memory_order_relaxed);
        }
        noiseFloor.store (0.0f, std::memory_order_relaxed);
    }
};
