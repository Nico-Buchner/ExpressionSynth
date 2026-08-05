#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FeatureExtractor.h"
#include "ExpressionMapper.h"
#include "ModulationState.h"
#include "PitchToMidiConverter.h"
#include "SynthEngine.h"

class ExpressionSynthProcessor : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener,
                                  private juce::AsyncUpdater
{
public:
    ExpressionSynthProcessor();
    ~ExpressionSynthProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto& in  = layouts.getMainInputChannelSet();
        const auto& out = layouts.getMainOutputChannelSet();

        if (in.isDisabled() || out.isDisabled())
            return false;

        const bool inOk  = in  == juce::AudioChannelSet::mono()
                        || in  == juce::AudioChannelSet::stereo();
        const bool outOk = out == juce::AudioChannelSet::mono()
                        || out == juce::AudioChannelSet::stereo();

        return inOk && outOk;
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ExpressionSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParams() { return apvts; }

    void setupDefaultRoutes();

    FeatureExtractor::Features getFeaturesForDisplay() const { return featureExtractor.getLatestFeatures(); }
    int getActiveNoteForDisplay() const { return pitchToMidi.getCurrentNote(); }
    juce::String getActiveProfileName() const { return pitchToMidi.getProfile().name; }
    const SpectrumData& getSpectrum() const { return featureExtractor.getSpectrum(); }
    const ExpressionMapper& getMapper() const { return expressionMapper; }

private:
    void parameterChanged (const juce::String& paramID, float newValue) override;
    void refreshArticulationProfile();
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState apvts;

    FeatureExtractor featureExtractor;
    ExpressionMapper expressionMapper;
    PitchToMidiConverter pitchToMidi;
    SynthEngine synthEngine;

    juce::AudioBuffer<float> analysisBuffer;
    juce::MidiBuffer generatedMidi;
    juce::SmoothedValue<float> pitchBendSmoother;
    ModulationState modulation;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExpressionSynthProcessor)
};
